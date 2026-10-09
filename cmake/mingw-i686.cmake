# cmake/mingw-i686.cmake
#
# Cross-compile the Windows version (32-bit, like the Linux build: the game
# assumes 32-bit pointers) from Linux with MinGW-w64.
#
#   sudo apt install gcc-mingw-w64-i686      # Debian/Ubuntu
#   tools/fetch_sdl2_mingw.sh                # SDL2 for MinGW, into deps/
#   cmake -S . -B build-win --toolchain cmake/mingw-i686.cmake
#   cmake --build build-win -j
#
# Upstream's tools (preproc, gbagfx, ...) are still built for and run on the
# Linux machine: tools/prepare_reference.py builds them with upstream's own
# make and the system compiler. With Wine installed, ctest runs the tests
# under it. cmake/mingw-x86_64.cmake is the 64-bit version.

set(CMAKE_SYSTEM_PROCESSOR x86)
set(_triple i686-w64-mingw32)
include("${CMAKE_CURRENT_LIST_DIR}/mingw-w64-common.cmake")
