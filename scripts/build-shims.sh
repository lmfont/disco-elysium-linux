#!/usr/bin/env bash
# build-shims.sh — build the two native shims into shims/.
#
#   libAmplifyTextureNative.so   AT2 virtual-texturing backend (C++; needs the
#                                pinned decoder sources from fetch-vendor.sh)
#   libPortGfxInfo.so            GL video-memory query (C; needs X11 + GL headers)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

command -v gcc >/dev/null 2>&1 || { echo "build-shims: gcc not found" >&2; exit 1; }

# --- gfxinfo -----------------------------------------------------------------
# Single source file: a plain timestamp check is enough here.
if [ ! -f "$ROOT/shims/libPortGfxInfo.so" ] \
   || [ "$ROOT/shims/gfxinfo/port_gfx_info.c" -nt "$ROOT/shims/libPortGfxInfo.so" ]; then
    echo "build-shims: building libPortGfxInfo.so"
    gcc -O2 -Wall -Wextra -fPIC -shared -o "$ROOT/shims/libPortGfxInfo.so" \
        "$ROOT/shims/gfxinfo/port_gfx_info.c" -lX11 -lGL
else
    echo "build-shims: libPortGfxInfo.so up to date"
fi

# --- AT2 ---------------------------------------------------------------------
command -v g++ >/dev/null 2>&1 || { echo "build-shims: g++ not found" >&2; exit 1; }
if [ ! -f "$ROOT/shims/at2/vendor/lzham/lzham.h" ] || [ ! -f "$ROOT/shims/at2/vendor/crn/crn_decomp.h" ]; then
    bash "$HERE/fetch-vendor.sh" decoders
fi
# The Makefile tracks its own object, header and vendor dependencies (with
# -MMD -MP), so always ask make instead of re-guessing timestamps here.
echo "build-shims: building libAmplifyTextureNative.so"
make -C "$ROOT/shims/at2" -s
install -m755 "$ROOT/shims/at2/libAmplifyTextureNative.so" "$ROOT/shims/libAmplifyTextureNative.so"

sha256sum "$ROOT/shims/libPortGfxInfo.so" "$ROOT/shims/libAmplifyTextureNative.so"
