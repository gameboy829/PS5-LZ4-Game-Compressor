#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/../ampr-ps5"
mkdir -p build
python3 standalone/embed.py
includes=(-I../ampr-native/reference/external/ampr_emu/include -I../ampr-native/reference/external/ampr_emu/third_party/lz4/lib -I../ampr-native/third_party/lz4)
gcc -O2 "${includes[@]}" -c ../ampr-native/reference/external/ampr_emu/third_party/lz4/lib/lz4.c -o build/host-lz4.o
gcc -O2 "${includes[@]}" -c ../ampr-native/third_party/lz4/lz4hc.c -o build/host-lz4hc.o
g++ -O2 -std=c++17 -pthread "${includes[@]}" '-DAMPR_STATE_ROOT="/tmp/lz4-independent-test"' -c src/ampr_backend.cpp -o build/host-backend.o
g++ -O2 -std=c++17 -pthread "${includes[@]}" -DSTUDIO_HOST_TEST -DSTUDIO_PORT=15911 -DSHADOW_PORT=10102 '-DAMPR_STATE_ROOT="/tmp/lz4-independent-test"' -c standalone/main.cpp -o build/host-service.o
g++ -pthread build/host-service.o build/host-backend.o build/host-lz4.o build/host-lz4hc.o -o build/host-service
g++ -O2 -std=c++17 -pthread "${includes[@]}" tests/profile_host.cpp build/host-lz4.o build/host-lz4hc.o -o build/profile-host
python3 tests/test_hash.py
python3 tests/test_standalone.py
python3 tests/test_profile_roundtrip.py
g++ -O2 -std=c++17 -pthread tests/original_host.cpp -o build/original-host
python3 tests/test_original.py
g++ -O2 -std=c++17 -pthread -Isrc tests/backend_production_host.cpp build/host-backend.o build/host-lz4.o build/host-lz4hc.o -o build/production-host
python3 tests/test_verification_options.py
