# de-linux-mod — build system.
#
#   make            build everything that is missing (BepInEx, plugin, shims)
#                   — every step is idempotent, and no Unity files are
#                   downloaded (the plugin compiles against the NuGet
#                   UnityEngine stub, like BepInEx itself)
#   make unity      pre-fetch the Unity player files (not needed to build;
#                   run.sh fetches them on first run)
#   make package    build everything, then create dist/de-linux-mod-<ver>.tar.gz
#   make clean      remove everything the build generated (the unity/ cache
#                   and the runtime output are left alone)
#
# Each step lives in scripts/ and skips itself when its output is present.
# The version comes from the VERSION file (overridable with VERSION=…).

SHELL := /bin/bash
VERSION := $(shell cat VERSION 2>/dev/null || echo 0.0.0)
export VERSION
.PHONY: all unity bepinex plugin shims package clean help

all: bepinex plugin shims

unity:
	@bash scripts/fetch-unity.sh

bepinex:
	@bash scripts/fetch-vendor.sh bepinex

plugin: bepinex
	@bash scripts/build-plugin.sh

shims:
	@bash scripts/build-shims.sh

package: all
	@bash scripts/package.sh

clean:
	@rm -rf loader libdoorstop.so dist plugin/bin plugin/obj
	@rm -f shims/*.so shims/at2/libAmplifyTextureNative.so
	@rm -rf shims/at2/build shims/at2/vendor
	@echo "clean: done (unity/ cache and runtime output left alone)"

help:
	@sed -n 's/^# \{0,1\}//p' Makefile | sed -n '1,12p'
