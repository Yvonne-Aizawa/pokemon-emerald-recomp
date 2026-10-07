#!/bin/sh
# Optimised build (-O2 with debug info, the default build type and what CI
# tests) in its own directory, so it doesn't reconfigure debug.sh's build/.
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-rel -j
./build-rel/pkmemerald "$@"
