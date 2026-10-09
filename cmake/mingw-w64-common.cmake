# cmake/mingw-w64-common.cmake
#
# What the MinGW-w64 toolchain files (mingw-i686.cmake, mingw-x86_64.cmake)
# share. They set _triple and CMAKE_SYSTEM_PROCESSOR, then include this.

set(CMAKE_SYSTEM_NAME Windows)

set(CMAKE_C_COMPILER   ${_triple}-gcc)
set(CMAKE_RC_COMPILER  ${_triple}-windres)
set(CMAKE_OBJCOPY      ${_triple}-objcopy CACHE FILEPATH "")
set(CMAKE_NM           ${_triple}-nm      CACHE FILEPATH "")

# Search the MinGW sysroot and deps/ for libraries and headers, never the
# Linux system. SDL2's MinGW package has a directory per triple.
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
