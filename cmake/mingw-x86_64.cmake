# cmake/mingw-x86_64.cmake
#
# Cross-compile the 64-bit Windows version (Phase 18, like the Linux build
# with -DPKM_HOST_32BIT=OFF) from Linux with MinGW-w64.
#
#   sudo apt install gcc-mingw-w64-x86-64    # Debian/Ubuntu
#   tools/fetch_sdl2_mingw.sh                # SDL2 for MinGW, into deps/
#   cmake -S . -B build-win64 --toolchain cmake/mingw-x86_64.cmake
#   cmake --build build-win64 -j
#
# As with cmake/mingw-i686.cmake, upstream's tools run on the Linux machine
# and ctest runs the tests under Wine (wine64).

set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(_triple x86_64-w64-mingw32)
include("${CMAKE_CURRENT_LIST_DIR}/mingw-w64-common.cmake")

# This compiler has no -m32: build the 64-bit host binary whatever
# PKM_HOST_32BIT was. CMakeLists.txt sets the -m32 flags before project(),
# which reads this file before it uses them.
set(PKM_HOST_32BIT OFF CACHE BOOL "Build a 32-bit host binary (matches GBA pointer size)" FORCE)
set(CMAKE_C_FLAGS_INIT "")
set(CMAKE_EXE_LINKER_FLAGS_INIT "")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "")
