# Third-party notices

This project redistributes the components below in its release packages:
the third-party packages are the pinned, sha256-verified downloads made by
`scripts/fetch-vendor.sh`, except for the decoder sources, which are
compiled at build time. Their license texts are included under `licenses/`
here and in every release package. No Unity engine files and no Disco
Elysium files are redistributed; the Unity player runtime is downloaded from
Unity's CDN by the end user (`scripts/fetch-unity.sh`), for their own use.

## BepInEx 5.4.23.3 — MIT

- Source: https://github.com/BepInEx/BepInEx
- Package: `BepInEx_linux_x64_5.4.23.3.zip` from the GitHub release
  `v5.4.23.3`, downloaded and sha256-verified by `scripts/fetch-vendor.sh`
- License: MIT (`licenses/BepInEx-MIT.txt`)
- The package's core assemblies are installed under `loader/core/`.

## UnityDoorstop 4.4.0 — LGPL-2.1

- Source: https://github.com/NeighTools/UnityDoorstop
- License: GNU Lesser General Public License v2.1 (`licenses/LGPL-2.1.txt`)
- Bundled in the pinned BepInEx release package; `scripts/fetch-vendor.sh`
  verifies the x64 `libdoorstop.so` against a sha256 before installing it.

## HarmonyX 2.9.0 (0Harmony) — MIT

- Source: https://github.com/BepInEx/HarmonyX (shipped by BepInEx via its
  BepInEx.Harmony project, https://github.com/BepInEx/BepInEx.Harmony)
- License: MIT (`licenses/HarmonyX-MIT.txt`; BepInEx.Harmony's own MIT text
  is `licenses/BepInEx.Harmony-MIT.txt`)
- Contained in the BepInEx release package and installed under
  `loader/core/` (`0Harmony.dll`, `HarmonyXInterop.dll`).

## MonoMod 22.1.29.1 (RuntimeDetour, Utils) — MIT

- Source: https://github.com/MonoMod/MonoMod
- License: MIT (`licenses/MonoMod-MIT.txt`)
- Contained in the BepInEx release package; installed under `loader/core/`.

## Mono.Cecil 0.10.4 — MIT

- Source: https://github.com/jbevain/cecil
- License: MIT (`licenses/Mono.Cecil-MIT.txt`)
- Contained in the BepInEx release package; installed under `loader/core/`.

## crunch  — Zlib

- Source: https://github.com/Diphome/UnityRift, commit
  `23c016c26bfd945e463f990d6377ec0a87ef27fe` (`Texture2DDecoderNative/unitycrunch`),
  upstream lineage: crunch by Richard Geldreich / Binomial LLC.
- License: zlib (`licenses/crunch-zlib.txt`)
- Fetched by `scripts/fetch-vendor.sh`, compiled into
  `shims/libAmplifyTextureNative.so`; not committed to this repository.

## LZHAM codec — public domain

- Source: https://github.com/richgel999/lzham_codec, commit
  `d379b1f9121e2197881c61cfc4713c78848bdfe7` (decompressor only).
- License: public domain (`licenses/LZHAM-public-domain.txt`)
- Fetched by `scripts/fetch-vendor.sh`, compiled into
  `shims/libAmplifyTextureNative.so`; not committed to this repository.

## Amplify Texture 2 native code — replaced by a shim, not included

- Disco Elysium ships Amplify Texture 2 as a managed assembly
  (`AmplifyTexture.dll`) plus a Windows-only native library
  (`AmplifyTextureNative.dll`); this project replaces only the native
  library with `shims/libAmplifyTextureNative.so`.
- License: proprietary.
- The native exports are reimplemented independently for interoperability;
  the PrePassHistogram functions are a clean-room implementation. No
  Amplify Texture 2 code is redistributed.
- Amplify Creations does not appear to have publicly offered Amplify
  Texture 2 for sale since 2017, or to have updated it since 2020.
