# libAmplifyTextureNative.so — Linux native backend for Amplify Texture 2.
#
# Builds our shim (src/) and the decoder wrappers (decode/) against the pinned
# third-party sources in vendor/ (crn: Unity's crunch fork, Zlib; lzham:
# public domain), which are fetched by scripts/fetch-vendor.sh — nothing
# third-party is committed to this repository.
#
# Object files are built per source with -MMD -MP, so header changes trigger
# a rebuild and the vendor warning suppressions only cover vendor/ code.
CXX ?= g++
CXXFLAGS ?= -O2 -g -fPIC -fno-strict-aliasing -pthread -include stdint.h
OUR_WARNFLAGS ?= -Wall -Wextra -Wno-unused-parameter
VENDOR_WARNFLAGS ?= -w -fpermissive
# vendor/ is a system include path so warnings from third-party headers are
# not attributed to our decode/ wrappers; VENDOR_WARNFLAGS covers .cpp files.
CPPFLAGS += -I src -I decode -isystem vendor -isystem vendor/lzham
LDFLAGS ?=

SRCS = src/amplify_texture_native.cpp src/bcn_shim.cpp src/prepass_histogram.cpp \
       decode/crn_decode.cpp decode/lzham_decode.cpp decode/lzham_shim.cpp
VENDOR_SRCS = $(wildcard vendor/lzham/*.cpp)

OUR_OBJS = $(patsubst src/%.cpp,build/our/src/%.o,$(filter src/%.cpp,$(SRCS))) \
           $(patsubst decode/%.cpp,build/our/decode/%.o,$(filter decode/%.cpp,$(SRCS)))
VENDOR_OBJS = $(patsubst vendor/%.cpp,build/vendor/%.o,$(VENDOR_SRCS))
DEPS = $(OUR_OBJS:.o=.d) $(VENDOR_OBJS:.o=.d)

all: libAmplifyTextureNative.so

libAmplifyTextureNative.so: $(OUR_OBJS) $(VENDOR_OBJS) Makefile
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o $@ $(OUR_OBJS) $(VENDOR_OBJS) -ldl

build/our/src/%.o: src/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OUR_WARNFLAGS) -MMD -MP -c $< -o $@

build/our/decode/%.o: decode/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OUR_WARNFLAGS) -MMD -MP -c $< -o $@

build/vendor/%.o: vendor/%.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(VENDOR_WARNFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f libAmplifyTextureNative.so
	rm -rf build

-include $(DEPS)

.PHONY: all clean
