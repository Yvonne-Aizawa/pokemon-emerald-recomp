###############################################################################
# pkmemerald — PC port of pokeemerald-expansion
#
# Phase 5: SDL2 window, events and frame timing, running the whole game
# (reference/src/*.c, data/*.s and the songs).
#
# This CMakeLists builds:
#   - pkmemerald-core  (STATIC)   the ported game code + platform layer
#   - pkmemerald       (executable) platform/src/host_main.c
#   - platform/tests/* (executables, HAL smoke tests; run with `ctest`)
#
# How a reference/src/*.c file is built (mirrors reference/Makefile):
#   1. tools/prepare_reference.py   upstream's host tools + generated headers +
#                                  converted graphics (once, see reference-prepare)
#   2. platform/patches/<f>.patch  optional host patch, applied to a copy
#   3. tools/host_preprocess.sh    host cpp | preproc  ->  <f>.i
#   4. host compiler               <f>.i -> object in pkmemerald-core
#
# Assembly *data* (data/*.s, sound/songs/midi/*.s -- no ARM code) is built by
# tools/host_assemble.sh with the host assembler.
###############################################################################

cmake_minimum_required(VERSION 3.20)

# The game assumes 32-bit pointers: it stores pointers in u32 task-data pairs
# (SetWordTaskArg), and static asserts struct sizes against GBA buffers
# (list_menu.c, recorded_battle.c). We therefore build a 32-bit host binary.
# This needs 32-bit libc headers and libgcc even to compile
# (Debian/Ubuntu: `gcc-multilib`), and a 32-bit SDL2 (`libsdl2-dev:i386`).
#
# Set before project() so CMake detects a 32-bit toolchain: that picks the
# i386 library directories (and so the 32-bit SDL2) in find_package.
option(PKM_HOST_32BIT "Build a 32-bit host binary (matches GBA pointer size)" ON)
if(PKM_HOST_32BIT)
    # SSE2 maths instead of x87: x87 keeps 80-bit intermediates or not
    # depending on optimisation, so the sound mix (host_audio.c) came out
    # slightly different between Debug and Release builds.
    set(CMAKE_C_FLAGS_INIT "-m32 -msse2 -mfpmath=sse")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")
endif()

project(pkmemerald
    VERSION 0.1.0
    LANGUAGES C
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

#-------------------------------------------------------------------------------
# Toolchain
#-------------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # pokeemerald uses GCC extensions (gnu17 upstream)

# Players get an optimised build with symbols (crash reports stay readable).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Build types only change optimisation. The game chooses its own debug or
# release configuration (upstream's RELEASE define turns on NDEBUG in
# config/general.h), and its sources are preprocessed separately, so they never
# see CMake's -DNDEBUG. Keep it out of the platform code too, so both halves
# agree on which debug-print functions exist (host_hal.c).
foreach(_cfg RELEASE RELWITHDEBINFO MINSIZEREL)
    string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_${_cfg} "${CMAKE_C_FLAGS_${_cfg}}")
endforeach()

# The assembled game data (data/*.s) holds absolute pointers in read-only
# sections, as on the GBA; a position-independent executable would need text
# relocations for them.
add_link_options(-no-pie)
if(WIN32)
    # The game tells code from data by address (host_gba_misc.c): code must
    # sit in the GBA's ROM window, 0x08000000-0x0A000000, like the Linux
    # non-PIE default. A fixed base without ASLR keeps it there.
    add_link_options(-Wl,--image-base,0x08000000 -Wl,--disable-dynamicbase)
    # The crash handler walks the stack through frame pointers
    # (crash_handler_win32.c).
    add_compile_options(-fno-omit-frame-pointer)
endif()

# The codebase was written for `-Wall -Wextra` under arm-none-eabi-gcc. We
# follow suit on the host, but suppress the warning floods that come from
# deliberately-unused-portable code (most of it, for now).
add_compile_options(
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
    -Wno-incompatible-pointer-types
    -Wno-int-conversion
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
)

#-------------------------------------------------------------------------------
# Layout
#-------------------------------------------------------------------------------
set(REFERENCE_DIR    "${CMAKE_SOURCE_DIR}/reference")
set(PLATFORM_DIR    "${CMAKE_SOURCE_DIR}/platform")
set(PLATFORM_INC    "${PLATFORM_DIR}/include")
set(REFERENCE_INC    "${REFERENCE_DIR}/include")
set(TOOLS_DIR       "${CMAKE_SOURCE_DIR}/tools")

#-------------------------------------------------------------------------------
# Host include tree
#
# Upstream headers include each other with quotes, e.g. global.h does
# `#include "gba/gba.h"`. GCC always searches the including file's own
# directory first for quoted includes, so -I/-iquote ordering can't make that
# resolve to our stub — it would always pick reference/include/gba/gba.h.
#
# Instead we build a mirror of reference/include/ out of symlinks, with `gba`
# pointing at platform/include/gba. GCC uses the path it found a header
# through (not its realpath) as "the including file's directory", so headers
# reached via this tree see the host stubs.
#-------------------------------------------------------------------------------
set(HOST_INC "${CMAKE_BINARY_DIR}/host_include")
file(REMOVE_RECURSE "${HOST_INC}")
file(MAKE_DIRECTORY "${HOST_INC}")
file(GLOB _ref_inc_entries LIST_DIRECTORIES true RELATIVE "${REFERENCE_INC}" "${REFERENCE_INC}/*")
foreach(_entry ${_ref_inc_entries})
    if(_entry STREQUAL "gba")
        file(CREATE_LINK "${PLATFORM_INC}/gba" "${HOST_INC}/gba" SYMBOLIC)
    else()
        file(CREATE_LINK "${REFERENCE_INC}/${_entry}" "${HOST_INC}/${_entry}" SYMBOLIC)
    endif()
endforeach()

#-------------------------------------------------------------------------------
# SDL2: window, rendering, events (and later input and audio)
#-------------------------------------------------------------------------------
find_package(SDL2 REQUIRED CONFIG)
message(STATUS "SDL2 found: ${SDL2_VERSION} (${SDL2_DIR})")

#-------------------------------------------------------------------------------
# Platform layer sources
#-------------------------------------------------------------------------------
file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${PLATFORM_DIR}/src/*.c"
)
# The entry point belongs to the executable, not the library.
set(HOST_MAIN_SOURCE "${PLATFORM_DIR}/src/host_main.c")
list(REMOVE_ITEM PLATFORM_SOURCES "${HOST_MAIN_SOURCE}")
# OS-specific pieces (interrupt timer, crash handler): *_posix.c/*_linux.c or
# *_win32.c.
if(WIN32)
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "_(posix|linux)\\.c$")
else()
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "_win32\\.c$")
endif()

# MinGW lays out bitfields the Microsoft way by default, which changes the
# game's struct sizes (its own static asserts catch it). Use GCC's layout
# everywhere except the files that share structs with Windows or SDL.
if(WIN32)
    add_compile_options(-mno-ms-bitfields)
    file(GLOB _os_api_sources "${PLATFORM_DIR}/src/*_sdl2.c" "${PLATFORM_DIR}/src/*_win32.c")
    set_source_files_properties(${_os_api_sources} PROPERTIES COMPILE_OPTIONS -mms-bitfields)
endif()

#-------------------------------------------------------------------------------
# reference/src/ sources
#
# Every top-level reference/src/*.c (the same set upstream compiles), minus the
# GBA-only files below. These are excluded permanently; where the game calls
# into them, platform/src/ provides a host replacement (noted per file) or
# a later phase will.
#-------------------------------------------------------------------------------
set(REFERENCE_SRC_EXCLUDE
    agb_flash               # GBA flash save            -> host_flash.c (Phase 8)
    agb_flash_1m            # GBA 1M flash              -> host_flash.c (Phase 8)
    agb_flash_le            # GBA flash (legacy)        -> host_flash.c (Phase 8)
    agb_flash_mx            # GBA flash (Macronix)      -> host_flash.c (Phase 8)
    AgbRfu_LinkManager      # GBA wireless link manager -> host_rfu.c (Phase 16)
    libisagbprn             # IS-AGB debug print        -> host_hal.c
    librfu_intr             # GBA link interrupt        -> host_rfu.c (Phase 16)
    librfu_rfu              # GBA RFU link              -> host_rfu.c (Phase 16)
    librfu_sio32id          # GBA SIO                   -> host_rfu.c (Phase 16)
    librfu_stwi             # GBA STWI                  -> host_rfu.c (Phase 16)
    multiboot               # GBA link-cable upload     -> host_multiboot.c
    siirtc                  # cartridge RTC via GPIO    -> host_rtc.c
    test_runner_battle      # GBA test runner (TESTING builds only)
)
# Also never built: src/*.s (ARM code: crt0, decompress_asm, libgcnmultiboot,
# rom_header -> host_gba_misc.c; m4a_1 -> m4a_engine.c).

file(GLOB _ref_c RELATIVE "${REFERENCE_DIR}" CONFIGURE_DEPENDS "${REFERENCE_DIR}/src/*.c")
set(REFERENCE_SRC)
foreach(_src ${_ref_c})
    get_filename_component(_name "${_src}" NAME_WE)
    if(NOT _name IN_LIST REFERENCE_SRC_EXCLUDE)
        list(APPEND REFERENCE_SRC "${_src}")
    endif()
endforeach()

# Assembly data, as upstream: every data/*.s, and one song per .mid (mid2agb
# turns sound/songs/midi/X.mid into X.s during reference-prepare).
file(GLOB REFERENCE_DATA_ASM RELATIVE "${REFERENCE_DIR}" CONFIGURE_DEPENDS "${REFERENCE_DIR}/data/*.s")
file(GLOB _ref_mid RELATIVE "${REFERENCE_DIR}" CONFIGURE_DEPENDS "${REFERENCE_DIR}/sound/songs/midi/*.mid")

#-------------------------------------------------------------------------------
# Step 1: upstream tools, generated headers and converted graphics
#
# Runs once (stamp file). Re-run after changing upstream graphics/data with
#     cmake --build build --target reference-prepare
#-------------------------------------------------------------------------------
set(PREPARE_CMD
    ${Python3_EXECUTABLE} "${TOOLS_DIR}/prepare_reference.py" "${REFERENCE_DIR}" ${REFERENCE_SRC} ${REFERENCE_DATA_ASM}
)
set(PREPARE_STAMP "${CMAKE_BINARY_DIR}/reference-prepare.stamp")
add_custom_command(
    OUTPUT "${PREPARE_STAMP}"
    COMMAND ${PREPARE_CMD}
    COMMAND ${CMAKE_COMMAND} -E touch "${PREPARE_STAMP}"
    DEPENDS "${TOOLS_DIR}/prepare_reference.py"
    COMMENT "Building upstream tools, generated headers and graphics"
    VERBATIM
)
add_custom_target(reference-prepare
    COMMAND ${PREPARE_CMD}
    COMMAND ${CMAKE_COMMAND} -E touch "${PREPARE_STAMP}"
    COMMENT "Re-running upstream tools, generated headers and graphics"
    VERBATIM
)

#-------------------------------------------------------------------------------
# Steps 2+3: patch (optional) and preprocess each source to a .i file
#-------------------------------------------------------------------------------
set(PREPROC_TOOL "${REFERENCE_DIR}/tools/preproc/preproc")
set(PATCH_DIR    "${PLATFORM_DIR}/patches")
set(PATCHED_DIR  "${CMAKE_BINARY_DIR}/reference_patched")
set(PP_DIR       "${CMAKE_BINARY_DIR}/reference_pp")
file(MAKE_DIRECTORY "${PATCHED_DIR}/src" "${PP_DIR}")

# Upstream's CPPFLAGS, plus HOST_BUILD and the host include tree. -iquote of
# reference/src lets patched copies (which live elsewhere) still find
# src-relative includes such as "data/trainers.h".
set(REFERENCE_CPPFLAGS
    -iquote "${HOST_INC}"
    -iquote "${REFERENCE_DIR}/src"
    -Wno-trigraphs
    -DMODERN=1
    -DTESTING=0
    -DEMERALD
    -DHOST_BUILD=1
    -std=gnu17
)
if(PKM_HOST_32BIT)
    list(APPEND REFERENCE_CPPFLAGS -m32)
endif()
if(WIN32)
    # POSIX headers MinGW lacks (alloca.h).
    list(APPEND REFERENCE_CPPFLAGS -isystem "${PLATFORM_DIR}/win32/include")
endif()

find_program(PATCH_EXECUTABLE patch REQUIRED)
# Which sources have a patch is decided at configure time; re-configure when
# a patch is added or removed.
file(GLOB _host_patches CONFIGURE_DEPENDS "${PATCH_DIR}/*.patch")
# Which sources have a patch is decided at configure time; re-configure when
# a patch is added or removed.
file(GLOB _host_patches CONFIGURE_DEPENDS "${PATCH_DIR}/*.patch")

set(REFERENCE_PP)
foreach(_src ${REFERENCE_SRC})
    get_filename_component(_name "${_src}" NAME_WE)
    set(_input "${REFERENCE_DIR}/${_src}")
    set(_patch "${PATCH_DIR}/${_name}.c.patch")

    if(EXISTS "${_patch}")
        set(_patched "${PATCHED_DIR}/${_src}")
        add_custom_command(
            OUTPUT "${_patched}"
            COMMAND ${PATCH_EXECUTABLE} --quiet --force -o "${_patched}" "${_input}" "${_patch}"
            DEPENDS "${_input}" "${_patch}"
            COMMENT "Patching ${_src}"
            VERBATIM
        )
        set(_input "${_patched}")
    endif()

    set(_out "${PP_DIR}/${_name}.i")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND bash "${TOOLS_DIR}/host_preprocess.sh"
                "${REFERENCE_DIR}" "${PREPROC_TOOL}" "${_out}" "${_out}.d" "${_src}" "${_input}"
                -- ${CMAKE_C_COMPILER} ${REFERENCE_CPPFLAGS}
        DEPENDS "${_input}" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_preprocess.sh"
        DEPFILE "${_out}.d"
        COMMENT "Preprocessing ${_src}"
        VERBATIM
    )
    list(APPEND REFERENCE_PP "${_out}")
endforeach()

# GCC treats .i as already-preprocessed C; tell CMake it's C too.
set_source_files_properties(${REFERENCE_PP} PROPERTIES LANGUAGE C GENERATED TRUE)

#-------------------------------------------------------------------------------
# Step 4: compile the game code
#-------------------------------------------------------------------------------
add_library(reference-objs OBJECT ${REFERENCE_PP})
# Debug aid: report shifts by >= the type's width at run time. ARM gives 0
# for those, x86 wraps the count, so each hit is a likely porting bug (see
# platform/patches/text.c.patch). Use a separate build directory:
#   cmake -S . -B build-ubsan -DPKM_SANITIZE_SHIFT=ON
option(PKM_SANITIZE_SHIFT "Instrument game code with -fsanitize=shift-exponent" OFF)
if(PKM_SANITIZE_SHIFT)
    target_compile_options(reference-objs PRIVATE -fsanitize=shift-exponent)
    add_link_options(-fsanitize=shift-exponent)
endif()

target_compile_options(reference-objs PRIVATE
    # CMake compiles the .i files with `-x c`; don't preprocess them again.
    -fpreprocessed
    # Upstream builds with -Wno-strict-aliasing and the code type-puns freely;
    # GBA code also assumes two's-complement wraparound.
    -fno-strict-aliasing
    -fwrapv
)

#-------------------------------------------------------------------------------
# Assembly data (data/*.s and songs)
#-------------------------------------------------------------------------------
set(ASM_DIR "${CMAKE_BINARY_DIR}/reference_asm")
file(MAKE_DIRECTORY "${ASM_DIR}/data" "${ASM_DIR}/songs")

set(REFERENCE_ASM_CPPFLAGS
    -iquote "${REFERENCE_INC}"
    -I "${REFERENCE_INC}"
    -Wno-trigraphs
    -DMODERN=1
    -DTESTING=0
    -DEMERALD
    -std=gnu17
)
set(HOST_ASFLAGS)
if(NOT WIN32)
    list(APPEND HOST_ASFLAGS -Wa,--noexecstack)   # ELF only
endif()
if(PKM_HOST_32BIT)
    list(APPEND REFERENCE_ASM_CPPFLAGS -m32)
    list(APPEND HOST_ASFLAGS -m32)
endif()

set(REFERENCE_ASM_OBJS)
foreach(_src ${REFERENCE_DATA_ASM})
    get_filename_component(_name "${_src}" NAME_WE)
    set(_out "${ASM_DIR}/data/${_name}.o")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E env HOST_NM=${CMAKE_NM} HOST_OBJCOPY=${CMAKE_OBJCOPY}
                bash "${TOOLS_DIR}/host_assemble.sh" data
                "${REFERENCE_DIR}" "${PREPROC_TOOL}" "${_out}" "${_out}.d" "${_src}"
                -- ${CMAKE_C_COMPILER} ${REFERENCE_ASM_CPPFLAGS}
                -- ${CMAKE_C_COMPILER} ${HOST_ASFLAGS}
        DEPENDS "${REFERENCE_DIR}/${_src}" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_assemble.sh"
        DEPFILE "${_out}.d"
        COMMENT "Assembling ${_src}"
        VERBATIM
    )
    list(APPEND REFERENCE_ASM_OBJS "${_out}")
endforeach()

foreach(_mid ${_ref_mid})
    get_filename_component(_name "${_mid}" NAME_WE)
    set(_out "${ASM_DIR}/songs/${_name}.o")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND} -E env HOST_NM=${CMAKE_NM} HOST_OBJCOPY=${CMAKE_OBJCOPY}
                bash "${TOOLS_DIR}/host_assemble.sh" song
                "${REFERENCE_DIR}" "${ASM_DIR}/sound" "${_out}" "sound/songs/midi/${_name}.s"
                -- ${CMAKE_C_COMPILER} ${HOST_ASFLAGS}
        DEPENDS "${REFERENCE_DIR}/${_mid}" "${REFERENCE_DIR}/sound/MPlayDef.s" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_assemble.sh"
        COMMENT "Assembling song ${_name}"
        VERBATIM
    )
    list(APPEND REFERENCE_ASM_OBJS "${_out}")
endforeach()

set_source_files_properties(${REFERENCE_ASM_OBJS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

#-------------------------------------------------------------------------------
# Target: pkmemerald-core (static lib)
#-------------------------------------------------------------------------------
add_library(pkmemerald-core STATIC
    ${PLATFORM_SOURCES}
    $<TARGET_OBJECTS:reference-objs>
    ${REFERENCE_ASM_OBJS}
)

target_include_directories(pkmemerald-core
    PUBLIC
        ${PLATFORM_INC}
)

# Game headers are reached through the host include tree (see above), as
# quoted includes only: reference/include/strings.h must not shadow libc's
# <strings.h>.
target_compile_options(pkmemerald-core
    PUBLIC
        -iquote "${HOST_INC}"
)

find_package(Threads REQUIRED)
target_link_libraries(pkmemerald-core PUBLIC SDL2::SDL2 Threads::Threads)
# Our main() is the entry point: SDL must not rename it to SDL_main (Windows).
target_compile_definitions(pkmemerald-core PUBLIC SDL_MAIN_HANDLED)

target_compile_definitions(pkmemerald-core
    PUBLIC
        MODERN=1
        HOST_BUILD=1   # lets source files #ifdef-out GBA-specific code
)

if(MSVC)
    target_compile_options(pkmemerald-core PRIVATE /utf-8)
endif()

#-------------------------------------------------------------------------------
# Test executables
#
# Each test under platform/tests/ becomes its own executable. Tests that open
# the platform layer run SDL's dummy video driver, so no display is needed.
#-------------------------------------------------------------------------------
file(GLOB PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS
    "${PLATFORM_DIR}/tests/*.c"
)
if(WIN32)
    # Forks and checks POSIX signals.
    list(FILTER PLATFORM_TEST_SOURCES EXCLUDE REGEX "/test_crash_handler\\.c$")
endif()
enable_testing()
set(HEADLESS_TEST_ENV "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
foreach(TEST_SRC ${PLATFORM_TEST_SOURCES})
    get_filename_component(TEST_NAME "${TEST_SRC}" NAME_WE)
    add_executable(${TEST_NAME} "${TEST_SRC}")
    target_link_libraries(${TEST_NAME} PRIVATE pkmemerald-core m)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "${HEADLESS_TEST_ENV}")
endforeach()

#-------------------------------------------------------------------------------
# Target: pkmemerald (executable)
#-------------------------------------------------------------------------------
add_executable(pkmemerald "${HOST_MAIN_SOURCE}")
# Link the *whole* library, not just what host_main reaches: every symbol the
# game references must resolve, so gaps show up at build time instead of as
# missing behaviour later.
target_link_libraries(pkmemerald PRIVATE
    -Wl,--whole-archive pkmemerald-core -Wl,--no-whole-archive
    m
)

# Boots the platform and the game, runs 60 game frames, exits 0.
add_test(NAME pkmemerald-boot COMMAND pkmemerald -f 60 -s "${CMAKE_BINARY_DIR}/test-saves")
set_tests_properties(pkmemerald-boot PROPERTIES
    PASS_REGULAR_EXPRESSION "boot ok\nsave: [^\n]*\ngame init ok\naudio: [^\n]*\nran 60 frames"
    ENVIRONMENT "${HEADLESS_TEST_ENV}"
)

# The host build must read upstream's assembled data exactly as the GBA does:
# identical bytes and relocations, and identical struct layouts. Checked
# against upstream's own ARM toolchain when it's installed.
find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc)
if(ARM_NONE_EABI_GCC)
    # The flags that decide struct layout, as the game is compiled with.
    set(_abi_flags)
    if(PKM_HOST_32BIT)
        set(_abi_flags -m32)
    endif()
    if(WIN32)
        list(APPEND _abi_flags -mno-ms-bitfields)
    endif()
    add_test(NAME data-abi-matches-gba
        COMMAND ${CMAKE_COMMAND} -E env HOST_OBJDUMP=${CMAKE_OBJDUMP} HOST_OBJCOPY=${CMAKE_OBJCOPY}
                ${Python3_EXECUTABLE} "${TOOLS_DIR}/verify_data_abi.py"
                "${REFERENCE_DIR}" "${CMAKE_BINARY_DIR}" ${CMAKE_C_COMPILER} ${_abi_flags}
    )
else()
    message(STATUS "arm-none-eabi-gcc not found: skipping the data-abi-matches-gba test")
endif()

#-------------------------------------------------------------------------------
# Install and packaging
#
# The game data is compiled into the executable, so installing is the
# executable plus a desktop launcher; saves go to the per-user data directory
# (Platform_GetUserDataDir). Packages are for the builder's own machines: the
# built game contains Nintendo's assets, so none are published (PLAN.md,
# Phase 17).
#
#   cmake --install build --prefix ~/.local
#   cmake --build build --target package     # local .tar.gz (.zip on Windows)
#-------------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS pkmemerald RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(WIN32)
    # SDL2.dll goes next to the .exe, in the build tree (for running it and
    # the tests) and in the install/zip.
    add_custom_command(TARGET pkmemerald POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:pkmemerald>"
        VERBATIM
    )
    install(FILES "$<TARGET_FILE:SDL2::SDL2>" DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(UNIX AND NOT APPLE)
    install(FILES "${PLATFORM_DIR}/linux/pkmemerald.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()

set(CPACK_PACKAGE_NAME "pkmemerald")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_SOURCE_GENERATOR "")
include(CPack)

#-------------------------------------------------------------------------------
# Reporting
#-------------------------------------------------------------------------------
list(LENGTH REFERENCE_SRC _n_ref)
message(STATUS "")
message(STATUS "pkmemerald Phase 5 build configuration:")
message(STATUS "  C compiler        : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  32-bit host       : ${PKM_HOST_32BIT}")
message(STATUS "  SDL2              : ${SDL2_VERSION}")
message(STATUS "  reference src/     : ${_n_ref} files")
message(STATUS "")
