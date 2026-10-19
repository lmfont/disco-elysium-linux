#!/usr/bin/env bash
# build.sh — build the Linux port runtime (thin wrapper around the Makefile).
#
#   ./build.sh [target...]      targets: all (default), unity, bepinex, plugin,
#                               shims, package, clean
#
# See README.md for the requirements (a .NET SDK is needed for the plugin).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec make -C "$HERE" "$@"
