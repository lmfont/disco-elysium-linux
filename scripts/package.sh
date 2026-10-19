#!/usr/bin/env bash
# package.sh — create the release package under dist/ (runtime only).
#
# The package contains our loader, plugin, shims, scripts and license files —
# no Unity files and no game files (those are fetched/provided by the user).
#
# The loader files are copied from an explicit allowlist (the pinned BepInEx
# core) plus PortRuntime.dll; anything else in loader/ is refused so a local
# plugin or an unexpected core file can never leak into a release.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
VERSION="${VERSION:-$(cat "$ROOT/VERSION" 2>/dev/null || echo 0.0.0)}"
NAME="de-linux-mod-$VERSION"
DIST="$ROOT/dist"

# BepInEx 5.4.23.3 core, pinned by scripts/fetch-vendor.sh.
CORE_FILES=(
    0Harmony.dll
    0Harmony.xml
    0Harmony20.dll
    BepInEx.Harmony.dll
    BepInEx.Harmony.xml
    BepInEx.Preloader.dll
    BepInEx.Preloader.xml
    BepInEx.dll
    BepInEx.xml
    HarmonyXInterop.dll
    Mono.Cecil.Mdb.dll
    Mono.Cecil.Pdb.dll
    Mono.Cecil.Rocks.dll
    Mono.Cecil.dll
    MonoMod.RuntimeDetour.dll
    MonoMod.RuntimeDetour.xml
    MonoMod.Utils.dll
    MonoMod.Utils.xml
)
PLUGIN_FILES=(PortRuntime.dll)

check_dir() { # dir allowed...
    local dir="$1"; shift
    local path name allowed matched
    while IFS= read -r path; do
        name="${path##*/}"
        matched=0
        for allowed in "$@"; do
            if [ "$name" = "$allowed" ]; then
                matched=1
                break
            fi
        done
        if [ "$matched" = 0 ]; then
            echo "package: unexpected entry in $dir: $path" >&2
            echo "         release packages are limited to the pinned loader files" >&2
            exit 1
        fi
    done < <(find "$dir" -mindepth 1 -maxdepth 1 -printf '%p\n')
}

[ -f "$ROOT/LICENSE" ] || { echo "package: no LICENSE — add the project's license first" >&2; exit 1; }
[ -d "$ROOT/licenses" ] || { echo "package: no licenses/ — third-party license texts are missing" >&2; exit 1; }

for f in "${CORE_FILES[@]}"; do
    [ -f "$ROOT/loader/core/$f" ] || { echo "package: missing loader/core/$f — run ./build.sh first" >&2; exit 1; }
done
for f in "${PLUGIN_FILES[@]}"; do
    [ -f "$ROOT/loader/plugins/$f" ] || { echo "package: missing loader/plugins/$f — run ./build.sh first" >&2; exit 1; }
done
for f in "$ROOT/libdoorstop.so" "$ROOT/shims/libAmplifyTextureNative.so" \
         "$ROOT/shims/libPortGfxInfo.so"; do
    [ -f "$f" ] || { echo "package: missing $f — run ./build.sh first" >&2; exit 1; }
done

check_dir "$ROOT/loader/core" "${CORE_FILES[@]}"
check_dir "$ROOT/loader/plugins" "${PLUGIN_FILES[@]}"

stage="$(mktemp -d "${TMPDIR:-/tmp}/de-linux-pkg.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$NAME/loader/core" "$stage/$NAME/loader/plugins" "$stage/$NAME/shims"

install -m755 "$ROOT/run.sh" "$stage/$NAME/run.sh"
install -m644 "$ROOT/README.md" "$ROOT/PORT-CHANGES.md" \
    "$ROOT/THIRD-PARTY-NOTICES.md" "$ROOT/LICENSE" "$stage/$NAME/"
cp -a "$ROOT/licenses" "$stage/$NAME/licenses"
mkdir -p "$stage/$NAME/scripts"
install -m755 "$ROOT/scripts/fetch-unity.sh" "$stage/$NAME/scripts/fetch-unity.sh"
install -m644 "$ROOT/scripts/pkg-extract.py" "$stage/$NAME/scripts/pkg-extract.py"
for f in "${CORE_FILES[@]}"; do
    install -m644 "$ROOT/loader/core/$f" "$stage/$NAME/loader/core/$f"
done
for f in "${PLUGIN_FILES[@]}"; do
    install -m644 "$ROOT/loader/plugins/$f" "$stage/$NAME/loader/plugins/$f"
done
install -m644 "$ROOT/libdoorstop.so" "$stage/$NAME/libdoorstop.so"
install -m755 "$ROOT/shims/libAmplifyTextureNative.so" "$ROOT/shims/libPortGfxInfo.so" "$stage/$NAME/shims/"

mkdir -p "$DIST"
tar -C "$stage" -czf "$DIST/$NAME.tar.gz" "$NAME"
sha256sum "$DIST/$NAME.tar.gz" | tee "$DIST/$NAME.tar.gz.sha256"
echo "package: $DIST/$NAME.tar.gz"
du -h "$DIST/$NAME.tar.gz"
