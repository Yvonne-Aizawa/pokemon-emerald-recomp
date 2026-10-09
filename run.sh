#!/bin/sh
# Build and run the game. Each configuration gets its own build directory,
# so switching doesn't reconfigure another (or debug.sh's build/).
#
#   ./run.sh [--debug] [--64] [--platform linux|windows] [GAME ARGS...]
#
#   --debug       Debug build (-O0) instead of the optimised one (-O2 with
#                 debug info, the default build type and what CI tests).
#   --64          64-bit host binary (Phase 18) instead of the 32-bit one.
#   --platform    linux (default), or windows: cross-built with MinGW-w64
#                 (cmake/mingw-i686.cmake, or mingw-x86_64.cmake with --64)
#                 and run under Wine.
#
# Anything else is passed to the game.
set -e

debug=0
bits=32
platform=linux
while [ $# -gt 0 ]; do
    case $1 in
    --debug) debug=1 ;;
    --64) bits=64 ;;
    --platform)
        [ $# -ge 2 ] || { echo "run.sh: --platform needs linux or windows" >&2; exit 2; }
        platform=$2
        shift
        ;;
    --platform=*) platform=${1#--platform=} ;;
    *) break ;;
    esac
    shift
done

case $platform in
linux) dir=build-rel ;;
windows) dir=build-win ;;
*) echo "run.sh: unknown platform '$platform' (linux or windows)" >&2; exit 2 ;;
esac
[ $bits = 64 ] && dir=${dir}64
[ $debug = 1 ] && dir=$dir-debug

if [ $debug = 1 ]; then
    config="-DCMAKE_BUILD_TYPE=Debug"
else
    config="-DCMAKE_BUILD_TYPE=RelWithDebInfo"
fi

if [ $platform = windows ]; then
    if [ $bits = 64 ]; then
        toolchain=cmake/mingw-x86_64.cmake
    else
        toolchain=cmake/mingw-i686.cmake
    fi
    ls -d deps/SDL2-* >/dev/null 2>&1 || tools/fetch_sdl2_mingw.sh
    cmake -S . -B "$dir" --toolchain "$toolchain" $config
    cmake --build "$dir" -j
    exec wine "./$dir/pkmemerald.exe" "$@"
fi

if [ $bits = 64 ]; then
    cmake -S . -B "$dir" $config -DPKM_HOST_32BIT=OFF
else
    cmake -S . -B "$dir" $config
fi
cmake --build "$dir" -j
exec "./$dir/pkmemerald" "$@"
