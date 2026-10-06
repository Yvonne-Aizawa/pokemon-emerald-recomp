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
# under it.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(_triple i686-w64-mingw32)
set(CMAKE_C_COMPILER   ${_triple}-gcc)
set(CMAKE_RC_COMPILER  ${_triple}-windres)
set(CMAKE_OBJCOPY      ${_triple}-objcopy CACHE FILEPATH "")
set(CMAKE_NM           ${_triple}-nm      CACHE FILEPATH "")

# Search the MinGW sysroot and deps/ for libraries and headers, never the
# Linux system.
file(GLOB _sdl2 "${CMAKE_CURRENT_LIST_DIR}/../deps/SDL2-*/${_triple}")
set(CMAKE_FIND_ROOT_PATH /usr/${_triple} ${_sdl2})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program(_wine wine)
if(_wine)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${_wine})
endif()
