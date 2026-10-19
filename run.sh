#!/usr/bin/env bash
# run.sh — run Disco Elysium (your Windows Mono copy) on Linux through the
# runtime loader. Your game files are never modified: the generated overlay
# lives under "game-overlay/" next to this script, so this release can be unpacked
# straight into the game folder.
#
#   ./run.sh [GAME_DIR] [extra player args...]
#
# GAME_DIR is the folder containing "Disco Elysium.exe" and
# "Disco Elysium_Data/" (your game, the Windows Mono build). If omitted, the
# current directory is checked, then this folder and its parents (for releases
# unpacked into the game folder), then the game folder used by a previous run.
#
# First run: the Unity 2020.3.12f1 Linux player files (~151 MB) are downloaded
# from Unity's CDN for your own use and verified; afterwards they are cached in
# unity/ and no network is needed. See README.md for details and terms.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VARIATION=linux64_withgfx_nondevelopment_mono
RUN="$HERE/game-overlay"

usage() {
    cat <<EOF
usage: $0 [GAME_DIR] [extra player args...]

GAME_DIR is the folder that contains your game files (the Windows Mono
build): "Disco Elysium.exe" and "Disco Elysium_Data/".
If omitted, the current directory is checked, then this folder and its
parents (unpack this release anywhere inside the game folder), then the
game folder used by the previous run.

examples:
    $0 /path/to/disco-elysium-2022-mono
    cd /path/to/disco-elysium-2022-mono && $HERE/run.sh
    $0 /path/to/game -screen-fullscreen 0     # extra args go to the player

The first run downloads the Unity 2020.3.12f1 Linux player runtime from
Unity's CDN (for your own use; nothing is redistributed) and caches it under
$HERE/unity/. Subsequent runs work offline.

The runtime overlay is generated under $HERE/game-overlay/; game files are never
modified.

If SDL_VIDEODRIVER=wayland is set in the environment, the system SDL2 is used
(through SDL_DYNAMIC_API) so that Unity's bundled, broken Wayland path is
avoided; when no system SDL2 is found, the game falls back to X11.
EOF
}

die() { echo "error: $*" >&2; echo >&2; usage >&2; exit 1; }

# --- resolve the game folder -------------------------------------------------
# A game folder is the Windows Mono build ("Disco Elysium.exe" plus
# "Disco Elysium_Data/"). The generated overlay (game-overlay/Disco Elysium_Data/)
# holds symlinks to the game's entries plus the Linux Mono runtime, so it
# never collides with the game's own data even when this release is unpacked
# into the game folder.
is_game_dir() { [ -f "$1/Disco Elysium.exe" ] && [ -d "$1/Disco Elysium_Data" ]; }

# game folder the previous run's overlay was built from (from its data links)
overlay_game_dir() {
    [ -d "$RUN/Disco Elysium_Data" ] || return 1
    local link target
    for link in "$RUN/Disco Elysium_Data"/*; do
        case "${link##*/}" in MonoBleedingEdge) continue ;; esac
        [ -L "$link" ] || continue
        target="$(readlink -- "$link")" || continue
        case "$target" in
            */"Disco Elysium_Data/"*)
                printf '%s\n' "${target%%/Disco Elysium_Data/*}"
                return 0 ;;
        esac
    done
    return 1
}

# nearest folder at or above this script that is a game folder
enclosing_game_dir() {
    local dir="$HERE"
    while :; do
        if is_game_dir "$dir"; then printf '%s\n' "$dir"; return 0; fi
        [ "$dir" = "/" ] && return 1
        dir="$(dirname "$dir")"
    done
}

GAME=""
if [ "$#" -ge 1 ] && [ "${1#-}" = "$1" ]; then
    # first argument is not a player option: it must be GAME_DIR
    if [ -d "$1" ]; then
        GAME="$(cd "$1" && pwd)"
        shift
    elif [ -e "$1" ]; then
        die "not a directory: $1"
    else
        die "game folder not found: $1"
    fi
fi

# GAME_DIR omitted (or the first argument is a player option): find the game
# folder — the current directory, this script's folder and its parents, the
# folder the existing overlay was built from, or the pre-overlay .game-dir.
if [ -z "$GAME" ]; then
    if is_game_dir "$PWD"; then
        GAME="$PWD"
    elif RECORDED="$(enclosing_game_dir)" && [ -n "$RECORDED" ]; then
        GAME="$RECORDED"
    elif RECORDED="$(overlay_game_dir)" && [ -n "$RECORDED" ]; then
        is_game_dir "$RECORDED" \
            || die "the game folder the overlay was built from is gone: $RECORDED"
        GAME="$RECORDED"
    else
        # upgrade path: the old layout kept the folder in a .game-dir file
        RECORDED=""
        if [ -f "$HERE/.game-dir" ]; then
            IFS= read -r RECORDED < "$HERE/.game-dir" || true
        fi
        if [ -n "$RECORDED" ] && is_game_dir "$RECORDED"; then
            GAME="$RECORDED"
        elif [ -n "$RECORDED" ]; then
            die "the game folder from the previous run is gone: $RECORDED (recorded in $HERE/.game-dir)"
        else
            die "no game folder: run from your game folder or pass GAME_DIR"
        fi
    fi
fi

[ -f "$GAME/Disco Elysium.exe" ] \
    || die "no \"Disco Elysium.exe\" in $GAME — GAME_DIR is your game folder (the Windows Mono build), not this mod folder"
[ -d "$GAME/Disco Elysium_Data" ] || die "no \"Disco Elysium_Data/\" in $GAME"

# --- Unity player files (download + verify on first run) ---------------------
bash "$HERE/scripts/fetch-unity.sh"

# --- build the overlay under game-overlay/ -------------------------------------------
# Everything generated lives here; the game folder is only read.
mkdir -p "$RUN"
if [ ! -x "$RUN/Disco Elysium.x86_64" ] \
   || ! cmp -s "$HERE/unity/$VARIATION/LinuxPlayer" "$RUN/Disco Elysium.x86_64"; then
    install -m755 "$HERE/unity/$VARIATION/LinuxPlayer" "$RUN/Disco Elysium.x86_64"
fi
ln -sfn "$HERE/unity/$VARIATION/UnityPlayer.so" "$RUN/UnityPlayer.so"

mkdir -p "$RUN/Disco Elysium_Data"
# refresh only symlinks: the real MonoBleedingEdge entry and nothing else stays
find "$RUN/Disco Elysium_Data" -mindepth 1 -maxdepth 1 -type l -delete
ln -sfn "$HERE/unity/$VARIATION/Data/MonoBleedingEdge" "$RUN/Disco Elysium_Data/MonoBleedingEdge"
while IFS= read -r entry; do
    [ "$entry" = "MonoBleedingEdge" ] && continue
    ln -sfn "$GAME/Disco Elysium_Data/$entry" "$RUN/Disco Elysium_Data/$entry"
done < <(find "$GAME/Disco Elysium_Data" -mindepth 1 -maxdepth 1 -printf '%f\n' | sort)
rm -f "$HERE/.game-dir"   # retired: the overlay records the game folder

# --- loader sanity -----------------------------------------------------------
[ -f "$HERE/loader/core/BepInEx.Preloader.dll" ] \
    || die "loader/ missing — run ./build.sh first, or use a release package"
[ -f "$HERE/libdoorstop.so" ] || die "libdoorstop.so missing — run ./build.sh first"
[ -f "$HERE/loader/plugins/PortRuntime.dll" ] || die "loader/plugins/PortRuntime.dll missing — run ./build.sh first"
[ -f "$HERE/shims/libAmplifyTextureNative.so" ] || die "shims missing — run ./build.sh first"

# --- launch (doorstop preloads BepInEx; the plugin patches at runtime) -------
# Video backend. Unity 2020.3 bundles an SDL2 whose Wayland path never opens a
# window (it spins on one core), so X11 is the default — it also covers
# Wayland sessions through XWayland. Some systems export
# SDL_VIDEODRIVER=wayland, though: keep that value but point SDL at the
# system SDL2 through SDL_DYNAMIC_API, whose Wayland backend works. If no
# system SDL2 can be found, fall back to X11 with a message.
find_system_sdl2() {
    local cand
    command -v ldconfig >/dev/null 2>&1 || return 1
    while IFS= read -r cand; do
        [ -f "$cand" ] || continue
        # A real x86_64 shared library: ELF class 2 (64-bit), type 3 (PIC).
        [ "$(od -An -tu1 -j4 -N1 "$cand" 2>/dev/null | tr -d ' ')" = 2 ] || continue
        [ "$(od -An -tu2 -j16 -N2 "$cand" 2>/dev/null | tr -d ' ')" = 3 ] || continue
        printf '%s\n' "$cand"
        return 0
    done < <(ldconfig -p 2>/dev/null | awk '/libSDL2-2\.0\.so\.0/ { print $NF }')
    return 1
}

video_first="${SDL_VIDEODRIVER:-}"
video_first="${video_first%%,*}"
if [ "$video_first" = wayland ] && [ -z "${SDL_DYNAMIC_API:-}" ]; then
    if SDL2="$(find_system_sdl2)"; then
        export SDL_DYNAMIC_API="$SDL2"
    else
        echo "== video:  SDL_VIDEODRIVER=wayland but no system SDL2 was found;" >&2
        echo "           using X11 instead (install SDL2 or set SDL_DYNAMIC_API)" >&2
        export SDL_VIDEODRIVER=x11
    fi
fi
if [ -z "${SDL_VIDEODRIVER:-}" ]; then
    export SDL_VIDEODRIVER=x11
fi

export DOORSTOP_ENABLED=1
export DOORSTOP_TARGET_ASSEMBLY="$HERE/loader/core/BepInEx.Preloader.dll"
export DOORSTOP_IGNORE_DISABLED_ENV=0
export LD_LIBRARY_PATH="$HERE:$HERE/shims:${LD_LIBRARY_PATH:-}"
if [ -n "${LD_PRELOAD:-}" ]; then
    export LD_PRELOAD="libdoorstop.so:$LD_PRELOAD"
else
    export LD_PRELOAD="libdoorstop.so"
fi

cd "$RUN"
echo "== game:   $GAME"
echo "== video:  ${SDL_VIDEODRIVER}${SDL_DYNAMIC_API:+ (system SDL2: $SDL_DYNAMIC_API)}"
echo "== loader: $HERE/loader   (log: $HERE/port.log)"
echo "== launching..."
exec "./Disco Elysium.x86_64" -logFile "$HERE/port.log" "$@"
