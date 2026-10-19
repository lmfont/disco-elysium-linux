# Port changes

What this project changes, fixes or adds to run the shipped Disco Elysium
**Mono build** (Unity 2020.3.12f1) on Linux — and how each piece is applied.
Unlike a source reconstruction, nothing here is compiled into the game: the
game's own assemblies and data are loaded unmodified and every platform fix
is applied **at runtime** by the loader. Implementations live in `plugin/`,
`shims/` and `scripts/`; usage is in `README.md`.

Each change is described once. Section 1–2 are the runtime layer, 3–5 are the
native and packaging pieces.

## 1. Loader and injection

- **BepInEx 5.4.23.3** is fetched by `scripts/fetch-vendor.sh` as BepInEx's
  own Linux x64 release package (GitHub release asset, sha256-verified — no
  BepInEx source or build toolchain is involved). The package carries
  **UnityDoorstop 4.4.0**; the fetch verifies its `libdoorstop.so` against a
  sha256 pin. Installed result:
  - `loader/core/` — BepInEx core assemblies + HarmonyX;
  - `libdoorstop.so` — the injector.
  (BepInEx derives its root from the preloader's location, so the loader can
  live in `loader/` instead of the conventional `BepInEx/` directory.)
- **`run.sh`** sets the Doorstop environment (`DOORSTOP_ENABLED`,
  `DOORSTOP_TARGET_ASSEMBLY=loader/core/BepInEx.Preloader.dll`,
  `LD_PRELOAD=libdoorstop.so`) and launches the player. Doorstop loads BepInEx
  before the first scene; `loader/plugins/PortRuntime.dll` applies every
  patch in §2. X11 is its default SDL video backend (`SDL_VIDEODRIVER=x11`):
  Unity 2020.3's bundled SDL2 Wayland path never opens a window (it spins on
  one core), and X11 also covers Wayland sessions through XWayland. When the
  environment asks for Wayland (`SDL_VIDEODRIVER=wayland`, as some systems
  export by default), `run.sh` keeps it but loads the system SDL2 through
  `SDL_DYNAMIC_API` (whose Wayland backend works); if no system SDL2 is found
  it falls back to `SDL_VIDEODRIVER=x11` with a message, and an already-set
  `SDL_DYNAMIC_API` is left alone.
- **The plugin is reflection-based**: it resolves each target type/method by
  name at runtime and logs (and skips) anything it cannot find. It therefore
  builds with no game files present and fails soft on unknown game builds.
- **Nothing Unity is needed to build**: the plugin compiles against the same
  compile-only NuGet `UnityEngine` stub that BepInEx itself references (for
  `BaseUnityPlugin`/`Debug`/`SystemInfo`), and its version is compiled in from
  the `VERSION` file. The Unity player files are fetched only at run time
  (§4).

## 2. Runtime patches (one file per patch, `plugin/patches/`)

`plugin/PortRuntime.cs` is the entry point: it captures the engine's
video-memory value, preloads the game assemblies and registers the six patch
files below. Each patch resolves its target by name (`PatchApplier`) and logs
(and skips) anything it cannot find.

- **Shader warmup storm** — `patches/WarmupPatch.cs`: prefix on
  `ShaderWarmupper.Warmup`, skip `Shader.WarmupAllShaders()`. The shipped
  shader blobs already carry the GL variant space; the call would make the
  driver compile thousands of programs at startup.
- **GL stencil format** — `patches/StencilPatch.cs`: transpiler on
  `DepthNormalsCamera.CheckResources`, nop the
  `stencilFormat = GraphicsFormat.R8_UInt` store. That format is D3D-only; on
  GL the depth-normals `RenderTexture.Create` failed every frame. The
  RenderTexture keeps its plain D24S8 depth buffer. The transpiler matches the
  exact `dup`/`ldc.i4.s 13`/`RenderTexture.set_stencilFormat` sequence and
  fails the patch (instead of partially editing IL) when it is not present
  exactly once.
- **Low-VRAM misdetection** — `patches/GraphicsMemoryPatch.cs`: prefix on
  `SystemInfo.get_graphicsMemorySize` returning `PortGfxInfo.VideoMemoryMB`
  (`plugin/PortGfxInfo.cs`, backed by the shim in §3). One patch point covers
  the game's own heuristics (`ShaderGraphicsOption`, `SunBehaviour`) *and*
  AmplifyTexture's streaming budget, so `AmplifyTexture.dll` stays
  byte-identical.
- **Teardown null guard** — `patches/HighlightGuardPatch.cs`: prefix on
  `MouseOverHighlight.RefreshHighlightMaterials`, skip when `renderers` is
  null (teardown paths such as `KimSkinSwapper.OnDestroy` can reach it before
  `Start()` assigned the field).
- **Windows-shaped AssetBundles folder** —
  `patches/AssetBundlePlatformPatch.cs`: prefix on
  `AddressablesTools.Utility.GetPlatformForAssetBundles` returning `Windows`:
  the shipped `StreamingAssets/AssetBundles/Windows` folder is used as-is (its
  bundles are platform-neutral) instead of renaming anything.
- **Windows-shaped Addressables paths** — `patches/InternalIdPatch.cs`:
  postfix on `ResourceManager.TransformInternalId` normalizing `\` to `/` in
  internal ids: the shipped `catalog.json` (which stores
  `…\StandaloneWindows64\….bundle` paths) is used unmodified.

The plugin also logs a platform line at startup
(`platform: renderer=…, AsyncGPUReadback=…`) — the latter decides which AT2
readback path the game uses (see §3).

## 3. Native shims (`shims/`)

- **`libAmplifyTextureNative.so`** — the Linux backend for Amplify Texture 2.
  Built from our `src/` and `decode/` wrappers against pinned decoder sources
  fetched by `scripts/fetch-vendor.sh` (Unity's crunch fork, Zlib licence;
  LZHAM, public domain). The managed `AmplifyTexture.dll` P/Invokes it by
  name; `run.sh` adds `shims/` to `LD_LIBRARY_PATH`, so the middleware
  assembly and the game folder stay untouched. Crunch output is checked
  against the decoder's page dimensions and maximum decoded size before it is
  copied.

  Besides the page decoders it implements AT2's **streaming visibility
  readback**. The middleware renders a per-frame "pre-pass" that encodes
  which virtual-texture pages are visible; on OpenGL there is no managed
  `AsyncGPUReadback` (the plugin logs `AsyncGPUReadback=False`), so the game
  always takes the native path: `IssuePrePassRead()` from the main thread,
  then `GL.IssuePluginEvent(GetRenderEventFunc(), 0)`, and later
  `GetPrePassPixels()` to collect the result. The shim's render-thread event
  uses a two-stage PBO ping-pong: each capture is followed by an
  `glFenceSync`, and a later frame polls it with `glClientWaitSync` and a
  zero timeout. Only a signaled fence is mapped (non-blocking), and while a
  fence is pending the frame is skipped instead of stalling the render
  thread; the mutex-guarded CPU buffer keeps the previous result. This frame's
  capture is queued into the other buffer with `glGetTexImage` on a bound
  `GL_PIXEL_PACK_BUFFER` (asynchronous DMA, with the last argument as a byte
  offset). All pack state the event touches — texture and pack-buffer
  bindings, pack swap bytes, alignment, row length and skip pixels/rows — is
  saved before lazy PBO creation and restored afterwards. The histogram is
  then built by
  `BuildPrePassHistogram`, a clean-room implementation in
  `shims/at2/src/prepass_histogram.{h,cpp}`. Without this readback the
  histogram is built from zeros: only the pages visible at scene load (read
  once through the managed preload path) ever stream in — the "initial area
  looks fine, textures fail to load elsewhere" symptom. Set `AT2_SHIM_DEBUG=1`
  to log readback sizes and non-zero pixel counts.
- **`libPortGfxInfo.so`** — GL video-memory query (X11/GLX). Unity 2020.3's
  GLCore backend ends up with a moving free-memory reading on Mesa (e.g.
  128 MB instead of the advertised amount); this library performs the
  `GLX_MESA_query_renderer` query the engine fails to make. The plugin
  consumes the value with a fallback to the engine's own value.

## 4. Unity player files (`scripts/fetch-unity.sh`)

Fetched **at run time** (never during the build):

- Downloads Unity's official **"Linux Mono support" package for the Mac
  editor** (151 MB) — the smallest official package that contains the
  `linux64_withgfx_nondevelopment_mono` PlaybackEngine variation — verifies
  it by sha256, and extracts only that variation.
- Re-verifies the two runtime binaries (`LinuxPlayer`, `UnityPlayer.so`)
  against known hashes and caches everything under `unity/`; re-runs are
  offline.
- Extraction uses `bsdtar`, or the dependency-free `scripts/pkg-extract.py`
  (xar + cpio reader) when bsdtar is unavailable.
- These files are downloaded from Unity's CDN for the user's own use; the
  project never redistributes them (see `README.md`).

## 5. The run overlay (`run.sh`)

- Builds a `game-overlay/` overlay next to `run.sh`: the launcher copy
  (`game-overlay/Disco Elysium.x86_64`), a `UnityPlayer.so` symlink, and
  `game-overlay/Disco Elysium_Data/` containing the Linux Mono runtime plus
  symlinks to the user's game data (the launcher does not write to those
  targets). Keeping the overlay in its own directory is what makes unpacking
  the release into the game folder safe.
- Symlinks are refreshed on every run; the launcher is only replaced when the
  source differs. The `unity/` cache, logs and BepInEx state live next to
  `run.sh`.
