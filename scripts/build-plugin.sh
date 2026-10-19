#!/usr/bin/env bash
# build-plugin.sh — compile the runtime patch plugin (loader/plugins/PortRuntime.dll).
#
# This is a standard .NET SDK build of plugin/PortRuntime.csproj: NuGet
# restore resolves the compile-only UnityEngine API stub (published by BepInEx
# on its feed) and the .NET Framework reference assemblies, with every version
# and content hash pinned by the committed packages.lock.json. The plugin
# resolves game types by name at runtime, so no game or Unity player files are
# needed here.
#
# References: the loader assemblies in loader/core/ (installed by ./build.sh);
# the plugin version comes from the VERSION file.
#
# Requirements: a .NET SDK 8 with dotnet on PATH.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
CSPROJ="$ROOT/plugin/PortRuntime.csproj"
OUT="$ROOT/loader/plugins/PortRuntime.dll"
VERSION="${VERSION:-$(cat "$ROOT/VERSION" 2>/dev/null || echo 0.0.0)}"

[ -f "$ROOT/loader/core/BepInEx.dll" ] || { echo "build-plugin: loader/core missing — run ./build.sh first" >&2; exit 1; }

# up to date? (rebuild when a source, the project or the lock is newer)
if [ -f "$OUT" ] && [ "$OUT" -nt "$ROOT/VERSION" ]; then
    if [ -z "$(find "$ROOT/plugin" \
            \( -name '*.cs' -o -name '*.csproj' -o -name 'packages.lock.json' \) \
            -newer "$OUT" 2>/dev/null | head -1)" ]; then
        echo "build-plugin: up to date ($OUT)"
        exit 0
    fi
fi

command -v dotnet >/dev/null 2>&1 \
    || { echo "build-plugin: dotnet not found — install the .NET SDK (see README.md)" >&2; exit 1; }

echo "build-plugin: building $(basename "$CSPROJ") (version $VERSION)"
dotnet restore "$CSPROJ" --locked-mode
dotnet build "$CSPROJ" -c Release --no-restore -p:Version="$VERSION"

mkdir -p "$(dirname "$OUT")"
install -m644 "$ROOT/plugin/bin/Release/PortRuntime.dll" "$OUT"
sha256sum "$OUT"
