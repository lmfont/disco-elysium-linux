# de-linux-mod — run Disco Elysium (Windows Mono build) on Linux

A runtime loader that runs the GOG/Steam **2022 Mono build** of Disco Elysium
- The Final Cut (Unity 2020.3.12f1) on Linux. It uses a generic Unity Linux
player runtime and applies the Linux port fixes **in memory** at startup —
your game files are never modified, and no game or Unity files are included
in this project or in its release packages.

```
./run.sh /path/to/disco-elysium-2022-mono
```

## Requirements

- x86_64 Linux with a graphical session (X11, or Wayland with XWayland) and
  working OpenGL drivers (Mesa for AMD/Intel, or NVIDIA).
- Your own copy of Disco Elysium, **Mono build** — the folder containing
  `Disco Elysium.exe` and `Disco Elysium_Data/` (e.g.
  `disco-elysium-2022-mono/`). You must legally own the game.
- To run a release package: `curl` (or `wget`) and `python3` (or `bsdtar`).
- To build from source: `git`, `make`, `gcc`/`g++`, a .NET SDK 8, `python3`
  and `curl` (or `wget`) for the one-time downloads, plus the X11 and OpenGL
  development headers used by the native shims (on Debian/Ubuntu:
  `libx11-dev libgl-dev`; on Fedora: `libX11-devel mesa-libGL-devel`).

## Usage

```
./run.sh [GAME_DIR] [extra player args...]
```

- `GAME_DIR` is the folder that contains your game files (the Windows Mono
  build: `Disco Elysium.exe` and `Disco Elysium_Data/`). If it is omitted,
  the current directory is checked, then `run.sh`'s own folder and its
  parents (so the release can be unpacked anywhere inside the game folder),
  then the game folder used by the previous run. If none works, a help text
  is shown.
- `run.sh` builds the runtime overlay (`Disco Elysium.x86_64`,
  `UnityPlayer.so` and a `Disco Elysium_Data/` of symlinks) under a
  `game-overlay/` folder next to itself; game files are never modified, and
  the release can be unpacked straight into the game folder.
- On first run the Unity 2020.3.12f1 Linux player files (~151 MB) are
  downloaded from Unity's CDN for your own use, verified and cached in
  `unity/`. Later runs are offline.
- If `SDL_VIDEODRIVER=wayland` is set in the environment (some systems do
  this by default), run.sh keeps it but uses the system SDL2 through
  `SDL_DYNAMIC_API` — the SDL2 bundled inside Unity 2020.3 has no working
  Wayland path. When no system SDL2 is found, it falls back to X11 with a
  message. Otherwise X11 stays the default (it also covers Wayland sessions
  through XWayland).
- Extra arguments are passed to the player (e.g. `-screen-fullscreen 0`).
- Player log: `port.log`; loader log: `loader/LogOutput.log`.

## Building from source

```
git clone <this repository>
cd de-linux-mod
./build.sh            # build everything that is missing (idempotent)
./build.sh package    # release package -> dist/de-linux-mod-<version>.tar.gz
```

The release package contains this project's code and the third-party
components listed in `THIRD-PARTY-NOTICES.md`.

## Legal

- **No Unity files are distributed.** The Unity player runtime is downloaded
  from Unity's CDN by `scripts/fetch-unity.sh`, on your machine, for your own
  use; Unity's terms apply to those files.
- **No Disco Elysium files are distributed.** You must legally own the game;
  this project only loads your existing copy.
- Third-party components and their licenses: `THIRD-PARTY-NOTICES.md` and
  the license texts under `licenses/`.
- This project's license: see `LICENSE`.

## Disclosure

This project acknowledges LLM assistance from Deepseek V4.1 Flash and Gemini
4 Argon.

## More detail

`PORT-CHANGES.md` describes what this port changes and how each piece works
(loader and injection, the runtime patches, the native shims, and how the
Unity player files are fetched).
