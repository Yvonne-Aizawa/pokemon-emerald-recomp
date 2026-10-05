###############################################################################
# pkmemerald — PC port of pokeemerald-expansion
#
# Phase 4: host entry point and main loop (headless).
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
    agb_flash               # GBA flash save            -> Phase 8
    agb_flash_1m            # GBA 1M flash              -> Phase 8
    agb_flash_le            # GBA flash (legacy)        -> Phase 8
    agb_flash_mx            # GBA flash (Macronix)      -> Phase 8
    AgbRfu_LinkManager      # GBA wireless link manager -> Phase 16
    libisagbprn             # IS-AGB debug print        -> host_hal.c
    main                    # AgbMain + GBA interrupts  -> host_main.c, main_loop.c
    librfu_intr             # GBA link interrupt        -> Phase 16
    librfu_rfu              # GBA RFU link              -> Phase 16
    librfu_sio32id          # GBA SIO                   -> Phase 16
    librfu_stwi             # GBA STWI                  -> Phase 16
    m4a                     # GBA audio engine          -> Phase 13
    m4a_tables              # GBA audio tables          -> Phase 13
    multiboot               # GBA link-cable upload     -> host_multiboot.c
    test_runner_battle      # GBA test runner (TESTING builds only)
)
# Also never built: src/*.s and asm/ (ARM assembly), data/*.s (assembled data
# — event scripts, maps; a later phase), data/mb_*.gba, sound/.

file(GLOB _ref_c RELATIVE "${REFRENCE_DIR}" CONFIGURE_DEPENDS "${REFRENCE_DIR}/src/*.c")
set(REFRENCE_SRC)
foreach(_src ${_ref_c})
    get_filename_component(_name "${_src}" NAME_WE)
    if(NOT _name IN_LIST REFRENCE_SRC_EXCLUDE)
        list(APPEND REFRENCE_SRC "${_src}")
    endif()
endforeach()

#-------------------------------------------------------------------------------
# Step 1: upstream tools, generated headers and converted graphics
#
# Runs once (stamp file). Re-run after changing upstream graphics/data with
#     cmake --build build --target refrence-prepare
#-------------------------------------------------------------------------------
set(PREPARE_CMD
    ${Python3_EXECUTABLE} "${TOOLS_DIR}/prepare_refrence.py" "${REFRENCE_DIR}" ${REFRENCE_SRC}
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
    # Upstream builds with -Wno-strict-aliasing and the code type-puns freely;
    # GBA code also assumes two's-complement wraparound.
    -fno-strict-aliasing
    -fwrapv
)

#-------------------------------------------------------------------------------
# Target: pkmemerald-core (static lib)
#-------------------------------------------------------------------------------
add_library(pkmemerald-core STATIC
    ${PLATFORM_SOURCES}
    $<TARGET_OBJECTS:refrence-objs>
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
target_link_libraries(pkmemerald PRIVATE pkmemerald-core)

# Phase 4 "done when": boots, runs 60 frames, exits 0.
add_test(NAME pkmemerald-boot COMMAND pkmemerald -f 60)
set_tests_properties(pkmemerald-boot PROPERTIES
    PASS_REGULAR_EXPRESSION "boot ok\nran 60 frames"
)

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
