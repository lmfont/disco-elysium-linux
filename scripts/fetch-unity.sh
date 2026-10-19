#!/usr/bin/env bash
# fetch-unity.sh — download and extract the Unity 2020.3.12f1 Linux player
# runtime ("engine shell") from Unity's CDN, for the user's own use.
#
# The files come from Unity's official "Linux Mono support" module package for
# the Mac editor — the smallest official package that contains the exact
# PlaybackEngine variation (linux64_withgfx_nondevelopment_mono) used by the
# engine-shell approach. Nothing is redistributed by this project: the package
# is downloaded by the user, verified by sha256, and cached under unity/.
#
# Result:
#   unity/linux64_withgfx_nondevelopment_mono/{LinuxPlayer,UnityPlayer.so,Data/...}
#
# Requirements: curl or wget; bsdtar or python3 (for extraction).
# Env: UNITY_EXTRACTOR=auto|bsdtar|python (default auto)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

VARIATION=linux64_withgfx_nondevelopment_mono
UNITY_DIR="$ROOT/unity"
PKG_URL="https://download.unity3d.com/download_unity/b3b2c6512326/MacEditorTargetInstaller/UnitySetup-Linux-Mono-Support-for-Editor-2020.3.12f1.pkg"
PKG_SHA256=376ea20d79e95380c7dc6c324eb1096b28aa32829806d327546ce9044495242d
PKG_CACHE="$UNITY_DIR/.cache/$(basename "$PKG_URL")"

# known-good hashes of the two runtime binaries (Unity 2020.3.12f1)
LINUXPLAYER_SHA256=95aa60b3bead39f7f6e86d1189664d420d8323243c63a45766601cd8fadeba88
UNITYPLAYER_SHA256=abad09b12f98af4505d8f67427f8c9c72e5e3b856bcd7ff7532828e86c57b9b7

verify_sha() { # file expected label
    local got
    got=$(sha256sum "$1" | cut -d' ' -f1)
    if [ "$got" != "$2" ]; then
        echo "fetch-unity: $3 checksum mismatch" >&2
        echo "  got      $got" >&2
        echo "  expected $2" >&2
        return 1
    fi
}

if [ -f "$UNITY_DIR/.complete" ] && [ -x "$UNITY_DIR/$VARIATION/LinuxPlayer" ]; then
    echo "fetch-unity: player files present ($UNITY_DIR/$VARIATION)"
    exit 0
fi

mkdir -p "$UNITY_DIR/.cache"

# --- download (cached; re-downloaded if the hash does not match) -------------
if [ ! -f "$PKG_CACHE" ] || ! verify_sha "$PKG_CACHE" "$PKG_SHA256" "package" 2>/dev/null; then
    echo "fetch-unity: downloading Unity 2020.3.12f1 Linux player files (~151 MB) from Unity's CDN"
    echo "             (UnitySetup-Linux-Mono-Support-for-Editor-2020.3.12f1.pkg, for your own use)"
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --progress-bar -o "$PKG_CACHE.part" "$PKG_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --show-progress -O "$PKG_CACHE.part" "$PKG_URL"
    else
        echo "fetch-unity: neither curl nor wget found" >&2
        exit 1
    fi
    mv "$PKG_CACHE.part" "$PKG_CACHE"
fi

verify_sha "$PKG_CACHE" "$PKG_SHA256" "package" || {
    echo "fetch-unity: removing bad download $PKG_CACHE" >&2
    rm -f "$PKG_CACHE"
    exit 1
}

# --- extract -----------------------------------------------------------------
UNITY_EXTRACTOR="${UNITY_EXTRACTOR:-auto}"
echo "fetch-unity: extracting $VARIATION ($UNITY_EXTRACTOR)"
use_bsdtar=0
case "$UNITY_EXTRACTOR" in
    bsdtar) use_bsdtar=1 ;;
    python) use_bsdtar=0 ;;
    auto) command -v bsdtar >/dev/null 2>&1 && use_bsdtar=1 ;;
    *) echo "fetch-unity: unknown UNITY_EXTRACTOR '$UNITY_EXTRACTOR' (use auto|bsdtar|python)" >&2; exit 1 ;;
esac
if [ "$use_bsdtar" = "1" ]; then
    command -v bsdtar >/dev/null 2>&1 || { echo "fetch-unity: UNITY_EXTRACTOR=bsdtar but bsdtar not found" >&2; exit 1; }
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/de-linux-unity.XXXXXX")"
    trap 'rm -rf "$tmp"' EXIT
    bsdtar -xf "$PKG_CACHE" -C "$tmp" --no-same-owner "TargetSupport.pkg.tmp/Payload"
    rm -rf "$UNITY_DIR/$VARIATION"
    bsdtar -xf "$tmp/TargetSupport.pkg.tmp/Payload" -C "$UNITY_DIR" --no-same-owner --strip-components 2 "./Variations/$VARIATION"
else
    command -v python3 >/dev/null 2>&1 || {
        echo "fetch-unity: need bsdtar or python3 to extract the package" >&2
        exit 1
    }
    rm -rf "$UNITY_DIR/$VARIATION"
    python3 "$HERE/pkg-extract.py" "$PKG_CACHE" "Variations/$VARIATION" "$UNITY_DIR/$VARIATION"
fi

# --- verify the extracted runtime --------------------------------------------
verify_sha "$UNITY_DIR/$VARIATION/LinuxPlayer" "$LINUXPLAYER_SHA256" "LinuxPlayer" || exit 1
verify_sha "$UNITY_DIR/$VARIATION/UnityPlayer.so" "$UNITYPLAYER_SHA256" "UnityPlayer.so" || exit 1
[ -d "$UNITY_DIR/$VARIATION/Data/MonoBleedingEdge/x86_64" ] || {
    echo "fetch-unity: MonoBleedingEdge missing in the extracted files" >&2
    exit 1
}

touch "$UNITY_DIR/.complete"
echo "fetch-unity: ready ($UNITY_DIR/$VARIATION)"
