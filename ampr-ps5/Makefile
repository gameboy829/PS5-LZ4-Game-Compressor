# SPDX-License-Identifier: GPL-3.0-or-later
SHELL := bash
ifndef PS5_PAYLOAD_SDK
$(error Set PS5_PAYLOAD_SDK)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
export LLVM_BINDIR ?= /usr/lib/llvm-18/bin
export LLVM_CONFIG ?= /usr/lib/llvm-18/bin/llvm-config
INCLUDES := -I../ampr-native/reference/external/ampr_emu/include -I../ampr-native/reference/external/ampr_emu/third_party/lz4/lib -I../ampr-native/third_party/lz4
CXXFLAGS := -O2 -std=c++17 -fexceptions -ffunction-sections -fdata-sections $(INCLUDES)
OBJECTS := build/launcher.o build/service.o build/backend.o build/lz4.o build/lz4hc.o
all: LZ4-Game-Compressor.elf
standalone/assets.h: build/update-helper.elf standalone/embed.py ../VERSION $(wildcard assets/*) $(wildcard assets-app/*)
	python3 standalone/embed.py
build:
	mkdir -p build
build/service.o: standalone/main.cpp standalone/http.h standalone/original.h standalone/thread.h standalone/library.h standalone/update_common.h standalone/https.h standalone/updater.inc standalone/report.inc standalone/assets.h | build
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/backend.o: src/ampr_backend.cpp src/ampr_benchmark.inc src/ampr_diagnostics.inc $(wildcard ../ampr-native/src/*) | build
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/lz4.o: ../ampr-native/reference/external/ampr_emu/third_party/lz4/lib/lz4.c | build
	$(CC) -O2 $(INCLUDES) -c $< -o $@
build/lz4hc.o: ../ampr-native/third_party/lz4/lz4hc.c | build
	$(CC) -O2 $(INCLUDES) -c $< -o $@
LZ4-Game-Compressor.elf: $(OBJECTS)
	$(CXX) -Wl,--gc-sections -o $@ $(OBJECTS) -lSceIpmi -lSceUserService -lSceSystemService -lSceNet -lSceSsl -lSceHttp -lSceAppInstUtil
standalone/launcher-assets.h: standalone/assets.h
build/launcher.o: standalone/launcher.cpp standalone/assets.h | build
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/shortcut-only.o: standalone/launcher.cpp standalone/assets.h | build
	$(CXX) $(CXXFLAGS) -DLZ4_SHORTCUT_ONLY -c $< -o $@
lz4-launcher-installer.elf: build/shortcut-only.o
	$(CXX) -Wl,--gc-sections -o $@ $< -lSceIpmi -lSceUserService -lSceSystemService -lSceAppInstUtil


build/update-helper.elf: standalone/update_helper.cpp standalone/update_common.h standalone/http.h | build
	$(CXX) $(CXXFLAGS) -o $@ $<
