#!/usr/bin/env bash
# fetch-vendor.sh — fetch the pinned third-party artifacts.
#
#   bepinex   the BepInEx 5.4.23.3 Linux x64 release package (GitHub release
#             asset), sha256-verified and installed as loader/core/ and
#             libdoorstop.so
#   decoders  decoder sources for the AT2 native shim, at pinned commits:
#               crn/   Unity's fork of Binomial's crunch 1.04 (Zlib licence),
#                      via the UnityRift repository
#               lzham/ richgel999/lzham_codec (public domain), decompressor
#                      only
#   all       both (default)
#
# Result:
#   loader/core/, loader/.doorstop_version, libdoorstop.so   (bepinex)
#   shims/at2/vendor/{crn,lzham}/                            (decoders)
#
# Nothing third-party is committed to this repository.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

WHAT="${1:-all}"
case "$WHAT" in
    all|bepinex|decoders) ;;
    *) echo "usage: fetch-vendor.sh [all|bepinex|decoders]" >&2; exit 1 ;;
esac

# ---------------------------------------------------------------- BepInEx -----
BEPINEX_VERSION=5.4.23.3
BEPINEX_URL="https://github.com/BepInEx/BepInEx/releases/download/v$BEPINEX_VERSION/BepInEx_linux_x64_$BEPINEX_VERSION.zip"
BEPINEX_SHA256=b9ef28b37676f18277cfd8dded01f675e934f207c30f65f2a4bb93c7e41abdbb
# the linux x64 libdoorstop.so bundled in the package (UnityDoorstop 4.4.0)
DOORSTOP_SHA256=853b2418dbfab8cd4ab0997ba883f210749d6f35cbc38970b0f3343bc28148a2

# ---------------------------------------------------------------- decoders ----
CRN_REPO=https://github.com/Diphome/UnityRift
CRN_COMMIT=23c016c26bfd945e463f990d6377ec0a87ef27fe
LZHAM_REPO=https://github.com/richgel999/lzham_codec
LZHAM_COMMIT=d379b1f9121e2197881c61cfc4713c78848bdfe7

WORK="$(mktemp -d "${TMPDIR:-/tmp}/de-linux-vendor.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

verify_sha() { # file expected label
    local got
    got=$(sha256sum "$1" | cut -d' ' -f1)
    if [ "$got" != "$2" ]; then
        echo "fetch-vendor: $3 checksum mismatch" >&2
        echo "  got      $got" >&2
        echo "  expected $2" >&2
        return 1
    fi
}

download() { # url dest
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --progress-bar -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --show-progress -O "$2" "$1"
    else
        echo "fetch-vendor: neither curl nor wget found" >&2
        exit 1
    fi
}

fetch_bepinex() {
    if [ -f "$ROOT/loader/core/BepInEx.dll" ] && [ -f "$ROOT/libdoorstop.so" ]; then
        echo "fetch-vendor: BepInEx loader present ($ROOT/loader/core)"
        return
    fi
    command -v python3 >/dev/null 2>&1 \
        || { echo "fetch-vendor: python3 is needed to extract the BepInEx package" >&2; exit 1; }

    echo "fetch-vendor: downloading BepInEx $BEPINEX_VERSION (GitHub release)"
    mkdir -p "$WORK/bepinex"
    download "$BEPINEX_URL" "$WORK/bepinex/package.zip"
    verify_sha "$WORK/bepinex/package.zip" "$BEPINEX_SHA256" "BepInEx package" || exit 1
    python3 -m zipfile -e "$WORK/bepinex/package.zip" "$WORK/bepinex/x"
    [ -d "$WORK/bepinex/x/BepInEx/core" ] \
        || { echo "fetch-vendor: unexpected BepInEx package layout" >&2; exit 1; }
    verify_sha "$WORK/bepinex/x/libdoorstop.so" "$DOORSTOP_SHA256" "libdoorstop.so" || exit 1

    rm -rf "$ROOT/loader"
    mkdir -p "$ROOT/loader/core" "$ROOT/loader/plugins"
    cp -a "$WORK/bepinex/x/BepInEx/core/." "$ROOT/loader/core/"
    install -m644 "$WORK/bepinex/x/libdoorstop.so" "$ROOT/libdoorstop.so"
    if [ -f "$WORK/bepinex/x/.doorstop_version" ]; then
        install -m644 "$WORK/bepinex/x/.doorstop_version" "$ROOT/loader/.doorstop_version"
    fi
    echo "fetch-vendor: BepInEx installed (loader/core, libdoorstop.so $(cat "$ROOT/loader/.doorstop_version" 2>/dev/null || echo "?"))"
}

fetch_commit() { # repo commit dest
    local repo=$1 commit=$2 dest=$3
    rm -rf "$dest"
    git init -q "$dest"
    git -C "$dest" remote add origin "$repo"
    git -C "$dest" fetch -q --depth 1 origin "$commit"
    git -C "$dest" checkout -q FETCH_HEAD
}

fetch_decoders() {
    local vendor="$ROOT/shims/at2/vendor"
    if [ -f "$vendor/lzham/lzham.h" ] && [ -f "$vendor/crn/crn_decomp.h" ]; then
        echo "fetch-vendor: decoder sources present ($vendor)"
        return
    fi
    echo "fetch-vendor: fetching pinned decoder sources"
    mkdir -p "$vendor" "$vendor/crn" "$vendor/lzham"

    fetch_commit "$CRN_REPO" "$CRN_COMMIT" "$WORK/unityrift"
    cp -a "$WORK/unityrift/Texture2DDecoderNative/unitycrunch/." "$vendor/crn/"

    fetch_commit "$LZHAM_REPO" "$LZHAM_COMMIT" "$WORK/lzham_codec"
    cp -a "$WORK/lzham_codec/include/lzham.h" "$vendor/lzham/"
    cp -a "$WORK/lzham_codec/lzhamdecomp/"*.h "$WORK/lzham_codec/lzhamdecomp/"*.cpp "$vendor/lzham/"

    echo "fetch-vendor: decoder sources ready ($vendor)"
}

if [ "$WHAT" = "all" ] || [ "$WHAT" = "bepinex" ]; then
    fetch_bepinex
fi
if [ "$WHAT" = "all" ] || [ "$WHAT" = "decoders" ]; then
    fetch_decoders
fi
