###############################################################################
# pkmemerald — PC port of pokeemerald-expansion
#
# Phase 4: host entry point and main loop (headless), linked against the
# whole game: refrence/src/*.c, data/*.s and the songs.
#
# This CMakeLists builds:
#   - pkmemerald-core  (STATIC)   the ported game code + platform layer
#   - pkmemerald       (executable) platform/src/host_main.c
#   - platform/tests/* (executables, HAL smoke tests; run with `ctest`)
#
# How a refrence/src/*.c file is built (mirrors refrence/Makefile):
#   1. tools/prepare_refrence.py   upstream's host tools + generated headers +
#                                  converted graphics (once, see refrence-prepare)
#   2. platform/patches/<f>.patch  optional host patch, applied to a copy
#   3. tools/host_preprocess.sh    host cpp | preproc  ->  <f>.i
#   4. host compiler               <f>.i -> object in pkmemerald-core
#
# Assembly *data* (data/*.s, sound/songs/midi/*.s -- no ARM code) is built by
# tools/host_assemble.sh with the host assembler.
###############################################################################

cmake_minimum_required(VERSION 3.20)
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

# The game assumes 32-bit pointers: it stores pointers in u32 task-data pairs
# (SetWordTaskArg), and static asserts struct sizes against GBA buffers
# (list_menu.c, recorded_battle.c). We therefore build a 32-bit host binary.
# This needs 32-bit libc headers and libgcc even to compile
# (Debian/Ubuntu: `gcc-multilib`).
option(PKM_HOST_32BIT "Build a 32-bit host binary (matches GBA pointer size)" ON)
if(PKM_HOST_32BIT)
    add_compile_options(-m32)
    add_link_options(-m32)
endif()

# The assembled game data (data/*.s) holds absolute pointers in read-only
# sections, as on the GBA; a position-independent executable would need text
# relocations for them.
add_link_options(-no-pie)

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
set(REFRENCE_DIR    "${CMAKE_SOURCE_DIR}/refrence")
set(PLATFORM_DIR    "${CMAKE_SOURCE_DIR}/platform")
set(PLATFORM_INC    "${PLATFORM_DIR}/include")
set(REFRENCE_INC    "${REFRENCE_DIR}/include")
set(TOOLS_DIR       "${CMAKE_SOURCE_DIR}/tools")

#-------------------------------------------------------------------------------
# Host include tree
#
# Upstream headers include each other with quotes, e.g. global.h does
# `#include "gba/gba.h"`. GCC always searches the including file's own
# directory first for quoted includes, so -I/-iquote ordering can't make that
# resolve to our stub — it would always pick refrence/include/gba/gba.h.
#
# Instead we build a mirror of refrence/include/ out of symlinks, with `gba`
# pointing at platform/include/gba. GCC uses the path it found a header
# through (not its realpath) as "the including file's directory", so headers
# reached via this tree see the host stubs.
#-------------------------------------------------------------------------------
set(HOST_INC "${CMAKE_BINARY_DIR}/host_include")
file(REMOVE_RECURSE "${HOST_INC}")
file(MAKE_DIRECTORY "${HOST_INC}")
file(GLOB _ref_inc_entries LIST_DIRECTORIES true RELATIVE "${REFRENCE_INC}" "${REFRENCE_INC}/*")
foreach(_entry ${_ref_inc_entries})
    if(_entry STREQUAL "gba")
        file(CREATE_LINK "${PLATFORM_INC}/gba" "${HOST_INC}/gba" SYMBOLIC)
    else()
        file(CREATE_LINK "${REFRENCE_INC}/${_entry}" "${HOST_INC}/${_entry}" SYMBOLIC)
    endif()
endforeach()

#-------------------------------------------------------------------------------
# Optional dependency: SDL2
#
# Not required until Phase 5. Note that a 32-bit build needs a 32-bit SDL2
# (Debian/Ubuntu: `libsdl2-dev:i386`).
#-------------------------------------------------------------------------------
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    message(STATUS "SDL2 found: ${SDL2_VERSION}")
else()
    message(STATUS "SDL2 not found — required in Phase 5 (window/renderer/input)")
endif()

#-------------------------------------------------------------------------------
# Platform layer sources
#-------------------------------------------------------------------------------
file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${PLATFORM_DIR}/src/*.c"
)
# The entry point belongs to the executable, not the library.
set(HOST_MAIN_SOURCE "${PLATFORM_DIR}/src/host_main.c")
list(REMOVE_ITEM PLATFORM_SOURCES "${HOST_MAIN_SOURCE}")

#-------------------------------------------------------------------------------
# refrence/src/ sources
#
# Every top-level refrence/src/*.c (the same set upstream compiles), minus the
# GBA-only files below. These are excluded permanently; where the game calls
# into them, platform/src/ provides a host replacement (noted per file) or
# a later phase will.
#-------------------------------------------------------------------------------
set(REFRENCE_SRC_EXCLUDE
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
    m4a                     # GBA audio engine          -> host_m4a.c (Phase 13)
    m4a_tables              # GBA audio tables          -> host_m4a.c (Phase 13)
    multiboot               # GBA link-cable upload     -> host_multiboot.c
    siirtc                  # cartridge RTC via GPIO    -> host_rtc.c
    test_runner_battle      # GBA test runner (TESTING builds only)
)
# Also never built: src/*.s (ARM code: crt0, m4a_1, decompress_asm,
# libgcnmultiboot, rom_header -> host_gba_misc.c).

file(GLOB _ref_c RELATIVE "${REFRENCE_DIR}" CONFIGURE_DEPENDS "${REFRENCE_DIR}/src/*.c")
set(REFRENCE_SRC)
foreach(_src ${_ref_c})
    get_filename_component(_name "${_src}" NAME_WE)
    if(NOT _name IN_LIST REFRENCE_SRC_EXCLUDE)
        list(APPEND REFRENCE_SRC "${_src}")
    endif()
endforeach()

# Assembly data, as upstream: every data/*.s, and one song per .mid (mid2agb
# turns sound/songs/midi/X.mid into X.s during refrence-prepare).
file(GLOB REFRENCE_DATA_ASM RELATIVE "${REFRENCE_DIR}" CONFIGURE_DEPENDS "${REFRENCE_DIR}/data/*.s")
file(GLOB _ref_mid RELATIVE "${REFRENCE_DIR}" CONFIGURE_DEPENDS "${REFRENCE_DIR}/sound/songs/midi/*.mid")

#-------------------------------------------------------------------------------
# Step 1: upstream tools, generated headers and converted graphics
#
# Runs once (stamp file). Re-run after changing upstream graphics/data with
#     cmake --build build --target refrence-prepare
#-------------------------------------------------------------------------------
set(PREPARE_CMD
    ${Python3_EXECUTABLE} "${TOOLS_DIR}/prepare_refrence.py" "${REFRENCE_DIR}" ${REFRENCE_SRC} ${REFRENCE_DATA_ASM}
)
set(PREPARE_STAMP "${CMAKE_BINARY_DIR}/refrence-prepare.stamp")
add_custom_command(
    OUTPUT "${PREPARE_STAMP}"
    COMMAND ${PREPARE_CMD}
    COMMAND ${CMAKE_COMMAND} -E touch "${PREPARE_STAMP}"
    DEPENDS "${TOOLS_DIR}/prepare_refrence.py"
    COMMENT "Building upstream tools, generated headers and graphics"
    VERBATIM
)
add_custom_target(refrence-prepare
    COMMAND ${PREPARE_CMD}
    COMMAND ${CMAKE_COMMAND} -E touch "${PREPARE_STAMP}"
    COMMENT "Re-running upstream tools, generated headers and graphics"
    VERBATIM
)

#-------------------------------------------------------------------------------
# Steps 2+3: patch (optional) and preprocess each source to a .i file
#-------------------------------------------------------------------------------
set(PREPROC_TOOL "${REFRENCE_DIR}/tools/preproc/preproc")
set(PATCH_DIR    "${PLATFORM_DIR}/patches")
set(PATCHED_DIR  "${CMAKE_BINARY_DIR}/refrence_patched")
set(PP_DIR       "${CMAKE_BINARY_DIR}/refrence_pp")
file(MAKE_DIRECTORY "${PATCHED_DIR}/src" "${PP_DIR}")

# Upstream's CPPFLAGS, plus HOST_BUILD and the host include tree. -iquote of
# refrence/src lets patched copies (which live elsewhere) still find
# src-relative includes such as "data/trainers.h".
set(REFRENCE_CPPFLAGS
    -iquote "${HOST_INC}"
    -iquote "${REFRENCE_DIR}/src"
    -Wno-trigraphs
    -DMODERN=1
    -DTESTING=0
    -DEMERALD
    -DHOST_BUILD=1
    -std=gnu17
)
if(PKM_HOST_32BIT)
    list(APPEND REFRENCE_CPPFLAGS -m32)
endif()

find_program(PATCH_EXECUTABLE patch REQUIRED)

set(REFRENCE_PP)
foreach(_src ${REFRENCE_SRC})
    get_filename_component(_name "${_src}" NAME_WE)
    set(_input "${REFRENCE_DIR}/${_src}")
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
                "${REFRENCE_DIR}" "${PREPROC_TOOL}" "${_out}" "${_out}.d" "${_src}" "${_input}"
                -- ${CMAKE_C_COMPILER} ${REFRENCE_CPPFLAGS}
        DEPENDS "${_input}" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_preprocess.sh"
        DEPFILE "${_out}.d"
        COMMENT "Preprocessing ${_src}"
        VERBATIM
    )
    list(APPEND REFRENCE_PP "${_out}")
endforeach()

# GCC treats .i as already-preprocessed C; tell CMake it's C too.
set_source_files_properties(${REFRENCE_PP} PROPERTIES LANGUAGE C GENERATED TRUE)

#-------------------------------------------------------------------------------
# Step 4: compile the game code
#-------------------------------------------------------------------------------
add_library(refrence-objs OBJECT ${REFRENCE_PP})
target_compile_options(refrence-objs PRIVATE
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
set(ASM_DIR "${CMAKE_BINARY_DIR}/refrence_asm")
file(MAKE_DIRECTORY "${ASM_DIR}/data" "${ASM_DIR}/songs")

set(REFRENCE_ASM_CPPFLAGS
    -iquote "${REFRENCE_INC}"
    -I "${REFRENCE_INC}"
    -Wno-trigraphs
    -DMODERN=1
    -DTESTING=0
    -DEMERALD
    -std=gnu17
)
set(HOST_ASFLAGS -Wa,--noexecstack)
if(PKM_HOST_32BIT)
    list(APPEND REFRENCE_ASM_CPPFLAGS -m32)
    list(APPEND HOST_ASFLAGS -m32)
endif()

set(REFRENCE_ASM_OBJS)
foreach(_src ${REFRENCE_DATA_ASM})
    get_filename_component(_name "${_src}" NAME_WE)
    set(_out "${ASM_DIR}/data/${_name}.o")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND bash "${TOOLS_DIR}/host_assemble.sh" data
                "${REFRENCE_DIR}" "${PREPROC_TOOL}" "${_out}" "${_out}.d" "${_src}"
                -- ${CMAKE_C_COMPILER} ${REFRENCE_ASM_CPPFLAGS}
                -- ${CMAKE_C_COMPILER} ${HOST_ASFLAGS}
        DEPENDS "${REFRENCE_DIR}/${_src}" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_assemble.sh"
        DEPFILE "${_out}.d"
        COMMENT "Assembling ${_src}"
        VERBATIM
    )
    list(APPEND REFRENCE_ASM_OBJS "${_out}")
endforeach()

foreach(_mid ${_ref_mid})
    get_filename_component(_name "${_mid}" NAME_WE)
    set(_out "${ASM_DIR}/songs/${_name}.o")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND bash "${TOOLS_DIR}/host_assemble.sh" song
                "${REFRENCE_DIR}" "${ASM_DIR}/sound" "${_out}" "sound/songs/midi/${_name}.s"
                -- ${CMAKE_C_COMPILER} ${HOST_ASFLAGS}
        DEPENDS "${REFRENCE_DIR}/${_mid}" "${REFRENCE_DIR}/sound/MPlayDef.s" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_assemble.sh"
        COMMENT "Assembling song ${_name}"
        VERBATIM
    )
    list(APPEND REFRENCE_ASM_OBJS "${_out}")
endforeach()

set_source_files_properties(${REFRENCE_ASM_OBJS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

#-------------------------------------------------------------------------------
# Target: pkmemerald-core (static lib)
#-------------------------------------------------------------------------------
add_library(pkmemerald-core STATIC
    ${PLATFORM_SOURCES}
    $<TARGET_OBJECTS:refrence-objs>
    ${REFRENCE_ASM_OBJS}
)

target_include_directories(pkmemerald-core
    PUBLIC
        ${PLATFORM_INC}
)

# Game headers are reached through the host include tree (see above), as
# quoted includes only: refrence/include/strings.h must not shadow libc's
# <strings.h>.
target_compile_options(pkmemerald-core
    PUBLIC
        -iquote "${HOST_INC}"
)

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
# Each test under platform/tests/ becomes its own executable. Tests are
# compile-and-run smoke tests for the host HAL stubs. They need a working
# link for the selected word size.
#-------------------------------------------------------------------------------
include(CheckCSourceCompiles)
if(PKM_HOST_32BIT)
    set(CMAKE_REQUIRED_FLAGS -m32)
endif()
check_c_source_compiles("int main(void) { return 0; }" PKM_CAN_LINK)
unset(CMAKE_REQUIRED_FLAGS)

file(GLOB PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS
    "${PLATFORM_DIR}/tests/*.c"
)
enable_testing()
if(PKM_CAN_LINK)
    foreach(TEST_SRC ${PLATFORM_TEST_SOURCES})
        get_filename_component(TEST_NAME "${TEST_SRC}" NAME_WE)
        add_executable(${TEST_NAME} "${TEST_SRC}")
        target_link_libraries(${TEST_NAME} PRIVATE pkmemerald-core m)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
else()
    message(WARNING
        "Cannot link ${CMAKE_SIZEOF_VOID_P}-byte-pointer host executables with -m32; "
        "skipping platform/tests. Install a 32-bit libc/libgcc (Debian/Ubuntu: gcc-multilib).")
endif()

#-------------------------------------------------------------------------------
# Target: pkmemerald (executable)
#
# SDL2 is not linked until Phase 5 (and must then be a 32-bit build when
# PKM_HOST_32BIT is on).
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
add_test(NAME pkmemerald-boot COMMAND pkmemerald -f 60)
set_tests_properties(pkmemerald-boot PROPERTIES
    PASS_REGULAR_EXPRESSION "boot ok\ngame init ok\nran 60 frames"
)

# The host build must read upstream's assembled data exactly as the GBA does:
# identical bytes and relocations, and identical struct layouts. Checked
# against upstream's own ARM toolchain when it's installed.
find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc)
find_program(GDB_EXECUTABLE gdb)
if(ARM_NONE_EABI_GCC AND GDB_EXECUTABLE)
    set(_abi_flags)
    if(PKM_HOST_32BIT)
        set(_abi_flags -m32)
    endif()
    add_test(NAME data-abi-matches-gba
        COMMAND ${Python3_EXECUTABLE} "${TOOLS_DIR}/verify_data_abi.py"
                "${REFRENCE_DIR}" "${CMAKE_BINARY_DIR}" ${CMAKE_C_COMPILER} ${_abi_flags}
    )
else()
    message(STATUS "arm-none-eabi-gcc or gdb not found: skipping the data-abi-matches-gba test")
endif()

#-------------------------------------------------------------------------------
# Reporting
#-------------------------------------------------------------------------------
list(LENGTH REFRENCE_SRC _n_ref)
message(STATUS "")
message(STATUS "pkmemerald Phase 4 build configuration:")
message(STATUS "  C compiler        : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  32-bit host       : ${PKM_HOST_32BIT}")
if(PKM_CAN_LINK)
    message(STATUS "  Can link tests    : yes")
else()
    message(STATUS "  Can link tests    : no (install gcc-multilib)")
endif()
message(STATUS "  refrence src/     : ${_n_ref} files")
message(STATUS "")
