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
# Linux x86-64 defaults to 0x00400000; tagged script pointers require code
# in the GBA ROM window, just as with the 32-bit executable.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_link_options(-Wl,-Ttext-segment=0x08000000)
endif()

# Sanitizers (PLAN.md, Phase 19a): e.g. -DPKM_SANITIZE=address,undefined
# reports reads through NULL or past an array -- which the GBA tolerates
# but a PC may not -- where they happen, even when nothing crashes. Linux
# only (MinGW has no sanitizer runtime). The crash handler stays out of the
# way in such builds (the sanitizer reports crashes itself).
set(PKM_SANITIZE "" CACHE STRING "Sanitizers to build with, e.g. address,undefined (Linux only)")
if(PKM_SANITIZE)
    if(WIN32)
        message(FATAL_ERROR "PKM_SANITIZE: MinGW has no sanitizer runtime; use the Linux build")
    endif()
    # Not shift-base: GCC defines signed left shifts into the sign bit (they
    # wrap), and upstream's DMA macros do that on every call. Shifts by the
    # width or more (shift-exponent) stay reported: those differ on x86.
    add_compile_options(-fsanitize=${PKM_SANITIZE} -fno-sanitize=shift-base
                        -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${PKM_SANITIZE})
    add_compile_definitions(PKM_SANITIZE=1)
    # With AddressSanitizer, the game's own heap marks its unused memory as
    # off-limits (malloc.c.patch, host_gba_misc.c).
    if(PKM_SANITIZE MATCHES "address")
        add_compile_definitions(PKM_ASAN=1)
    endif()
endif()
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
# upstream's code (which we don't edit). Our own code (platform/) turns the
# serious ones back on: PLATFORM_WARNINGS below.
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

# platform/ (the library's own sources, the tests, the entry point): all of
# the above, and errors with PKM_WERROR (on in CI). Off by default, so a
# newer compiler's new warnings don't stop anyone building the game.
option(PKM_WERROR "Treat warnings in platform/ code as errors" OFF)
set(PLATFORM_WARNINGS
    -Wunused-variable
    -Wunused-function
    -Wunused-but-set-variable
    -Wimplicit-function-declaration
    -Wincompatible-pointer-types
    -Wint-conversion
    -Wpointer-to-int-cast
    -Wint-to-pointer-cast
)
if(PKM_WERROR)
    list(APPEND PLATFORM_WARNINGS -Werror)
endif()

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
#
# `test` (upstream's test framework, Phase 19b) is a directory of links, so
# the host versions in platform/upstream_tests/include/test/ can replace
# single headers.
#-------------------------------------------------------------------------------
set(HOST_INC "${CMAKE_BINARY_DIR}/host_include")
file(REMOVE_RECURSE "${HOST_INC}")
file(MAKE_DIRECTORY "${HOST_INC}")
file(GLOB _ref_inc_entries LIST_DIRECTORIES true RELATIVE "${REFERENCE_INC}" "${REFERENCE_INC}/*")
foreach(_entry ${_ref_inc_entries})
    if(_entry STREQUAL "gba")
        file(CREATE_LINK "${PLATFORM_INC}/gba" "${HOST_INC}/gba" SYMBOLIC)
    elseif(_entry STREQUAL "test")
        file(MAKE_DIRECTORY "${HOST_INC}/test")
        file(GLOB _test_headers RELATIVE "${REFERENCE_INC}/test" "${REFERENCE_INC}/test/*")
        foreach(_header ${_test_headers})
            set(_target "${REFERENCE_INC}/test/${_header}")
            if(EXISTS "${PLATFORM_DIR}/upstream_tests/include/test/${_header}")
                set(_target "${PLATFORM_DIR}/upstream_tests/include/test/${_header}")
            endif()
            if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND (_header STREQUAL "test.h" OR _header STREQUAL "battle.h"))
                # PC port: GCC otherwise aligns the 40-byte Test records to
                # 32 bytes, inserting gaps in the linker-collected array.
                file(READ "${_target}" _test_header_text)
                string(REPLACE "section(\".tests\"), used" "section(\".tests\"), aligned(sizeof(void *)), used"
                    _test_header_text "${_test_header_text}")
                file(WRITE "${HOST_INC}/test/${_header}" "${_test_header_text}")
            else()
                file(CREATE_LINK "${_target}" "${HOST_INC}/test/${_header}" SYMBOLIC)
            endif()
        endforeach()
    elseif(_entry STREQUAL "metaprogram.h")
        # PC port: the null-dereference trick inside __builtin_constant_p
        # triggers UBSan. choose_expr tests compile-time truth without UB.
        file(READ "${REFERENCE_INC}/${_entry}" _metaprogram_text)
        string(REPLACE "__builtin_constant_p((cond) ? 0 : *(int *)0)"
            "__builtin_choose_expr(__builtin_constant_p(cond), (cond), 0)"
            _metaprogram_text "${_metaprogram_text}")
        file(WRITE "${HOST_INC}/${_entry}" "${_metaprogram_text}")
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
# Upstream's test suite (Phase 19b; built further below): its tests can
# INCBIN assets of their own (test/compression/), prepared with the rest.
option(PKM_UPSTREAM_TESTS "Build upstream's test suite as pkmemerald-tests (Linux)" OFF)
set(UPSTREAM_TEST_SOURCES)
set(_prepare_variant "")
if(PKM_UPSTREAM_TESTS)
    if(WIN32)
        message(FATAL_ERROR "PKM_UPSTREAM_TESTS: Linux only for now (PLAN.md, 19b-5)")
    endif()
    # The runner, and the tests run so far (19b-1; 19b-2: the non-battle
    # tests, test/*.c and test/compression/; 19b-3 and 19b-4: all of
    # test/battle/).
    file(GLOB _upstream_tests RELATIVE "${REFERENCE_DIR}" CONFIGURE_DEPENDS
        "${REFERENCE_DIR}/test/*.c"
        "${REFERENCE_DIR}/test/compression/*.c")
    file(GLOB_RECURSE _upstream_battle_tests RELATIVE "${REFERENCE_DIR}" CONFIGURE_DEPENDS
        "${REFERENCE_DIR}/test/battle/*.c")
    list(APPEND _upstream_tests ${_upstream_battle_tests})
    # The runner itself, listed first; test_runner_args.c: see below.
    list(REMOVE_ITEM _upstream_tests
        test/test_runner.c test/test_runner_battle.c test/test_runner_args.c)
    set(UPSTREAM_TEST_SOURCES
        test/test_runner.c
        test/test_runner_battle.c
        ${_upstream_tests}
    )
    # Its own stamp, so turning the option on prepares the tests' assets.
    set(_prepare_variant "-tests")
endif()

set(PREPARE_CMD
    ${Python3_EXECUTABLE} "${TOOLS_DIR}/prepare_reference.py" "${REFERENCE_DIR}" ${REFERENCE_SRC} ${REFERENCE_DATA_ASM}
    ${UPSTREAM_TEST_SOURCES}
)
set(PREPARE_STAMP "${CMAKE_BINARY_DIR}/reference-prepare${_prepare_variant}.stamp")
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
# The game sources are preprocessed apart from the compile step, so the
# sanitizer defines (above) must be passed here too.
if(PKM_SANITIZE)
    list(APPEND REFERENCE_CPPFLAGS -DPKM_SANITIZE=1)
    if(PKM_SANITIZE MATCHES "address")
        list(APPEND REFERENCE_CPPFLAGS -DPKM_ASAN=1)
    endif()
endif()

find_program(PATCH_EXECUTABLE patch REQUIRED)
# Which sources have a patch is decided at configure time; re-configure when
# a patch is added or removed.
file(GLOB_RECURSE _host_patches CONFIGURE_DEPENDS "${PATCH_DIR}/*.patch")

# pkm_preprocess(OUT_VAR VARIANT TESTING SOURCE...): patch each upstream
# source (when platform/patches/ has <path without "src/">.patch, e.g.
# patches/x.c.patch for src/x.c, patches/test/x.c.patch for test/x.c) into a
# copy, and preprocess it to a .i file with -DTESTING=<TESTING>. VARIANT is
# appended to the output directories, so the game and the test build
# (Phase 19b) don't share files.
function(pkm_preprocess out_var variant testing)
    set(_patched_dir "${CMAKE_BINARY_DIR}/reference_patched${variant}")
    set(_pp_dir "${CMAKE_BINARY_DIR}/reference_pp${variant}")
    file(MAKE_DIRECTORY "${_pp_dir}")
    set(_flags ${REFERENCE_CPPFLAGS})
    list(TRANSFORM _flags REPLACE "^-DTESTING=0$" "-DTESTING=${testing}")
    # __FILE__ as upstream sees it ("src/x.c", "test/x.c"): the test runner
    # matches test files by it, and messages stay short.
    list(APPEND _flags "-fmacro-prefix-map=${REFERENCE_DIR}/=" "-fmacro-prefix-map=${_patched_dir}/=")
    if(testing)
        # As -iquote reference/src does for patched game files: test-relative
        # includes ("battle/trainer_control.h") from patched test files; and
        # host headers the test runner shares with them
        # ("platform/host_test_ram.h").
        list(APPEND _flags -iquote "${REFERENCE_DIR}/test" -iquote "${PLATFORM_DIR}/include")
    endif()
    set(_outputs)
    foreach(_src ${ARGN})
        string(REGEX REPLACE "^src/" "" _rel "${_src}")
        set(_input "${REFERENCE_DIR}/${_src}")
        set(_patch "${PATCH_DIR}/${_rel}.patch")

        if(EXISTS "${_patch}")
            set(_patched "${_patched_dir}/${_src}")
            get_filename_component(_patched_parent "${_patched}" DIRECTORY)
            file(MAKE_DIRECTORY "${_patched_parent}")
            add_custom_command(
                OUTPUT "${_patched}"
                COMMAND ${PATCH_EXECUTABLE} --quiet --force -o "${_patched}" "${_input}" "${_patch}"
                DEPENDS "${_input}" "${_patch}"
                COMMENT "Patching ${_src}"
                VERBATIM
            )
            set(_input "${_patched}")
        endif()

        # src/x.c -> x.i; test/a/b.c -> test__a__b.i (test files may share a
        # base name with game files).
        string(REGEX REPLACE "\\.c$" ".i" _flat "${_rel}")
        string(REPLACE "/" "__" _flat "${_flat}")
        set(_out "${_pp_dir}/${_flat}")
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND bash "${TOOLS_DIR}/host_preprocess.sh"
                    "${REFERENCE_DIR}" "${PREPROC_TOOL}" "${_out}" "${_out}.d" "${_src}" "${_input}"
                    -- ${CMAKE_C_COMPILER} ${_flags}
            DEPENDS "${_input}" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_preprocess.sh"
            DEPFILE "${_out}.d"
            COMMENT "Preprocessing ${_src}"
            VERBATIM
        )
        list(APPEND _outputs "${_out}")
    endforeach()

    # GCC treats .i as already-preprocessed C; tell CMake it's C too.
    set_source_files_properties(${_outputs} PROPERTIES LANGUAGE C GENERATED TRUE)
    set(${out_var} ${_outputs} PARENT_SCOPE)
endfunction()

pkm_preprocess(REFERENCE_PP "" 0 ${REFERENCE_SRC})

#-------------------------------------------------------------------------------
# Step 4: compile the game code
#-------------------------------------------------------------------------------
# Debug aid: report shifts by >= the type's width at run time. ARM gives 0
# for those, x86 wraps the count, so each hit is a likely porting bug (see
# platform/patches/text.c.patch). Use a separate build directory:
#   cmake -S . -B build-ubsan -DPKM_SANITIZE_SHIFT=ON
option(PKM_SANITIZE_SHIFT "Instrument game code with -fsanitize=shift-exponent" OFF)
if(PKM_SANITIZE_SHIFT)
    add_link_options(-fsanitize=shift-exponent)
endif()

# pkm_reference_objects(TARGET SOURCE...): the preprocessed game code,
# compiled.
function(pkm_reference_objects target)
    add_library(${target} OBJECT ${ARGN})
    if(PKM_SANITIZE_SHIFT)
        target_compile_options(${target} PRIVATE -fsanitize=shift-exponent)
    endif()
    target_compile_options(${target} PRIVATE
        # CMake compiles the .i files with `-x c`; don't preprocess them again.
        -fpreprocessed
        # Upstream builds with -Wno-strict-aliasing and the code type-puns
        # freely; GBA code also assumes two's-complement wraparound.
        -fno-strict-aliasing
        -fwrapv
    )
endfunction()

pkm_reference_objects(reference-objs ${REFERENCE_PP})

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
        DEPENDS "${REFERENCE_DIR}/${_src}" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_assemble.sh" "${TOOLS_DIR}/host_data_asm.py"
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
        DEPENDS "${REFERENCE_DIR}/${_mid}" "${REFERENCE_DIR}/sound/MPlayDef.s" "${PREPARE_STAMP}" "${TOOLS_DIR}/host_assemble.sh" "${TOOLS_DIR}/host_data_asm.py"
        COMMENT "Assembling song ${_name}"
        VERBATIM
    )
    list(APPEND REFERENCE_ASM_OBJS "${_out}")
endforeach()

set_source_files_properties(${REFERENCE_ASM_OBJS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

#-------------------------------------------------------------------------------
# Target: pkmemerald-core (static lib)
#-------------------------------------------------------------------------------
file(GLOB SAVE_SYMBOL_HEADERS CONFIGURE_DEPENDS "${REFERENCE_INC}/constants/flags*.h" "${REFERENCE_INC}/constants/vars*.h")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/save_symbols.inc"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/platform/tools/save_symbols.py"
            "${REFERENCE_DIR}" "${CMAKE_BINARY_DIR}/save_symbols.inc"
    DEPENDS "${CMAKE_SOURCE_DIR}/platform/tools/save_symbols.py" ${SAVE_SYMBOL_HEADERS} "${REFERENCE_DIR}/charmap.txt"
    VERBATIM)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/save_abi.inc"
    COMMAND ${Python3_EXECUTABLE} "${TOOLS_DIR}/save_abi.py"
            "${PLATFORM_DIR}/src/host_save_layout.inc" "${PLATFORM_DIR}/src/host_save_layout_64.inc"
            "${CMAKE_BINARY_DIR}/save_abi.inc"
    DEPENDS "${TOOLS_DIR}/save_abi.py" "${PLATFORM_DIR}/src/host_save_layout.inc"
            "${PLATFORM_DIR}/src/host_save_layout_64.inc"
    VERBATIM)
# The map list for --visit-maps (host_visit_maps.c).
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/map_names.inc"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/platform/tools/map_names.py"
            "${REFERENCE_INC}/constants/map_groups.h" "${CMAKE_BINARY_DIR}/map_names.inc"
    DEPENDS "${CMAKE_SOURCE_DIR}/platform/tools/map_names.py" "${PREPARE_STAMP}"
    VERBATIM)

# Game headers are reached through the host include tree (see above). As an
# -idirafter directory it is searched after the system headers, so
# reference/include/strings.h can't shadow libc's <strings.h>, and its headers
# count as system headers: upstream's warnings don't land in platform/ code
# (PLATFORM_WARNINGS). Game headers include each other from their own
# directory first, so only a header platform/ includes directly could be
# shadowed by a libc header of the same name (none is; it would fail to
# compile, not misbehave). -fno-canonical-system-headers: GCC otherwise
# resolves system headers' symlinks, which would bypass the host include
# tree (global.h would pick up upstream's gba/ headers instead of ours).
#
# pkm_core_library(TARGET OBJECTS_TARGET): the platform layer, the compiled
# game code (OBJECTS_TARGET) and the assembled game data.
find_package(Threads REQUIRED)
function(pkm_core_library target objects)
    add_library(${target} STATIC
        ${PLATFORM_SOURCES}
        "${CMAKE_BINARY_DIR}/save_symbols.inc"
        "${CMAKE_BINARY_DIR}/save_abi.inc"
        "${CMAKE_BINARY_DIR}/map_names.inc"
        $<TARGET_OBJECTS:${objects}>
        ${REFERENCE_ASM_OBJS}
    )
    target_include_directories(${target} PUBLIC ${PLATFORM_INC} PRIVATE "${CMAKE_BINARY_DIR}")
    target_compile_options(${target}
        PUBLIC
            -idirafter "${HOST_INC}"
            -fno-canonical-system-headers
        PRIVATE
            ${PLATFORM_WARNINGS}
    )
    target_link_libraries(${target} PUBLIC SDL2::SDL2 Threads::Threads)
    target_compile_definitions(${target}
        PUBLIC
            # Our main() is the entry point: SDL must not rename it to
            # SDL_main (Windows).
            SDL_MAIN_HANDLED
            MODERN=1
            HOST_BUILD=1   # lets source files #ifdef-out GBA-specific code
    )
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

pkm_core_library(pkmemerald-core reference-objs)

# The save blocks' layout, for the JSON save file's field table
# (platform/src/host_save_layout.inc, generated by platform/tools/save_layout.py
# from this object's debug info and committed). Compiled like the platform
# layer, never linked. After upstream changes a save struct, the table's
# checks fail to compile: run `cmake --build build --target save-layout`
# (needs pyelftools and a Linux build).
add_library(save-layout-probe OBJECT "${PLATFORM_DIR}/tools/save_layout_probe.c")
target_include_directories(save-layout-probe PRIVATE ${PLATFORM_INC})
target_compile_options(save-layout-probe PRIVATE -g -idirafter "${HOST_INC}" -fno-canonical-system-headers)
target_compile_definitions(save-layout-probe PRIVATE MODERN=1 HOST_BUILD=1)
add_dependencies(save-layout-probe reference-objs)
# Pointer-bearing save structs have different layouts on 32- and 64-bit hosts.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SAVE_LAYOUT_INC "${PLATFORM_DIR}/src/host_save_layout_64.inc")
else()
    set(SAVE_LAYOUT_INC "${PLATFORM_DIR}/src/host_save_layout.inc")
endif()
# A Python with pyelftools: CMake's own, else the system's python3.
set(SAVE_LAYOUT_PYTHON "")
find_program(_system_python3 NAMES python3 PATHS /usr/bin NO_DEFAULT_PATH)
foreach(_python IN ITEMS "${Python3_EXECUTABLE}" "${_system_python3}")
    if(_python AND NOT SAVE_LAYOUT_PYTHON)
        execute_process(COMMAND "${_python}" -c "import elftools"
                        RESULT_VARIABLE _no_pyelftools OUTPUT_QUIET ERROR_QUIET)
        if(_no_pyelftools EQUAL 0)
            set(SAVE_LAYOUT_PYTHON "${_python}")
        endif()
    endif()
endforeach()
if(NOT SAVE_LAYOUT_PYTHON)
    message(STATUS "pyelftools not found: the save-layout target and test are unavailable")
    set(SAVE_LAYOUT_PYTHON "${Python3_EXECUTABLE}")
    set(_save_layout_unavailable TRUE)
endif()
add_custom_target(save-layout
    COMMAND "${SAVE_LAYOUT_PYTHON}" "${PLATFORM_DIR}/tools/save_layout.py"
            $<TARGET_OBJECTS:save-layout-probe> "${SAVE_LAYOUT_INC}"
    DEPENDS save-layout-probe
    VERBATIM)

#-------------------------------------------------------------------------------
# Test executables
#
# Each test under platform/tests/ becomes its own executable. Tests that open
# the platform layer run SDL's dummy video driver, so no display is needed.
#-------------------------------------------------------------------------------
file(GLOB PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS
    "${PLATFORM_DIR}/tests/*.c"
)
if(WIN32 OR PKM_SANITIZE)
    # Forks and checks POSIX signals; sanitizer builds report crashes
    # themselves (crash_handler_linux.c stays out of the way).
    list(FILTER PLATFORM_TEST_SOURCES EXCLUDE REGEX "/test_crash_handler\\.c$")
endif()
enable_testing()
add_test(NAME host-data-assembly
    COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tests/test_host_data_asm.py")
add_test(NAME checkpoint-setup-regression
    COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tests/test_restore_checkpoints.py")

add_test(NAME save_archive_roundtrip
    COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tests/test_save_archive.py")

add_test(NAME save_json_export
    COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tests/test_save_json.py")

set(HEADLESS_TEST_ENV "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
foreach(TEST_SRC ${PLATFORM_TEST_SOURCES})
    get_filename_component(TEST_NAME "${TEST_SRC}" NAME_WE)
    add_executable(${TEST_NAME} "${TEST_SRC}")
    target_link_libraries(${TEST_NAME} PRIVATE pkmemerald-core m)
    target_compile_options(${TEST_NAME} PRIVATE ${PLATFORM_WARNINGS})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "${HEADLESS_TEST_ENV}")
endforeach()

foreach(_invalid_time IN ITEMS
    "2026-02-30 12:00:00" "2026-02-29 12:00:00" "2024-02-29 12:34:56junk"
    "2024-02-29 24:00:00" "2024-02-29 12:60:00" "2024-02-29 12:34:60"
    "2024-00-29 12:34:56" "2024-13-29 12:34:56" "2024-02-00 12:34:56"
    "2024-04-31 12:34:56" "1999-12-31 12:34:56" "2100-01-01 12:34:56"
    "2024-2-29 12:34:56" "999999999999-02-29 12:34:56")
    string(MAKE_C_IDENTIFIER "${_invalid_time}" _time_id)
    add_test(NAME "rtc-invalid-${_time_id}" COMMAND test_rtc "${_invalid_time}" --invalid)
endforeach()

#-------------------------------------------------------------------------------
# Target: pkmemerald (executable)
#-------------------------------------------------------------------------------
add_executable(pkmemerald "${HOST_MAIN_SOURCE}")
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME script_file_long_lines
        COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tests/test_script_file.py" $<TARGET_FILE:pkmemerald>)
    add_test(NAME save_inspector_rejects_invalid
        COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tests/test_save_inspector.py" $<TARGET_FILE:pkmemerald>)
endif()

target_compile_options(pkmemerald PRIVATE ${PLATFORM_WARNINGS})
# Windows: a windowed program, so no console window opens beside the game;
# --console shows one (console_win32.c).
set_target_properties(pkmemerald PROPERTIES WIN32_EXECUTABLE TRUE)
# Link the *whole* library, not just what host_main reaches: every symbol the
# game references must resolve, so gaps show up at build time instead of as
# missing behaviour later.
target_link_libraries(pkmemerald PRIVATE
    -Wl,--whole-archive pkmemerald-core -Wl,--no-whole-archive
    m
)

# Reviewed user-played JSON archives reconstruct isolated, disposable saves.
set(CHECKPOINT_MANIFEST "${PLATFORM_DIR}/tests/fixtures/checkpoints/manifest.json")
set(CHECKPOINT_OUTPUT "${CMAKE_BINARY_DIR}/checkpoint-saves")
add_test(NAME checkpoint-restore
    COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tools/restore_checkpoints.py"
            "${CHECKPOINT_MANIFEST}" "${CHECKPOINT_OUTPUT}")
set_tests_properties(checkpoint-restore PROPERTIES FIXTURES_SETUP played-checkpoints)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHECKPOINT_MANIFEST}")
file(READ "${CHECKPOINT_MANIFEST}" _checkpoint_manifest)
string(JSON _checkpoint_count LENGTH "${_checkpoint_manifest}" checkpoints)
math(EXPR _checkpoint_last "${_checkpoint_count} - 1")
foreach(_index RANGE ${_checkpoint_last})
    string(JSON _checkpoint_id GET "${_checkpoint_manifest}" checkpoints ${_index} id)
    add_test(NAME "checkpoint-load-${_checkpoint_id}"
        COMMAND pkmemerald --inspect-save "${CHECKPOINT_OUTPUT}/${_checkpoint_id}/pkmemerald.sav")
    set_tests_properties("checkpoint-load-${_checkpoint_id}" PROPERTIES
        FIXTURES_REQUIRED played-checkpoints
        PASS_REGULAR_EXPRESSION "\"loader_status\":1"
        ENVIRONMENT "${HEADLESS_TEST_ENV};ASAN_OPTIONS=detect_leaks=0;UBSAN_OPTIONS=print_stacktrace=1")
endforeach()
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME checkpoint-reviewed-state
        COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tests/test_checkpoint_states.py"
                "${CHECKPOINT_MANIFEST}" "${CHECKPOINT_OUTPUT}" $<TARGET_FILE:pkmemerald>)
    set_tests_properties(checkpoint-reviewed-state PROPERTIES
        FIXTURES_REQUIRED played-checkpoints
        ENVIRONMENT "${HEADLESS_TEST_ENV};ASAN_OPTIONS=detect_leaks=0;UBSAN_OPTIONS=print_stacktrace=1")
    # The committed save field table matches this build's structs.
    if(NOT _save_layout_unavailable)
        add_test(NAME save-layout-up-to-date
            COMMAND "${SAVE_LAYOUT_PYTHON}" "${PLATFORM_DIR}/tools/save_layout.py"
                    $<TARGET_OBJECTS:save-layout-probe> "${SAVE_LAYOUT_INC}" --check)
    endif()
    # The JSON save format: conversion, edits, refusals, raw fallback, migration.
    add_test(NAME json-save-format
        COMMAND ${Python3_EXECUTABLE} "${PLATFORM_DIR}/tests/test_json_save_format.py"
                "${CHECKPOINT_MANIFEST}" "${CHECKPOINT_OUTPUT}" $<TARGET_FILE:pkmemerald>)
    set_tests_properties(json-save-format PROPERTIES
        FIXTURES_REQUIRED played-checkpoints
        ENVIRONMENT "${HEADLESS_TEST_ENV};ASAN_OPTIONS=detect_leaks=0;UBSAN_OPTIONS=print_stacktrace=1")
endif()

# Every map (PLAN.md, Phase 19c): --visit-maps warps to each map in turn from
# a played checkpoint, and fails on a crash, a sanitizer report (in a
# PKM_SANITIZE build) or a map it can't reach. Split by map group into parts
# of about the same size. Each part gets its own copy of the save (the game
# converts it in place). Not under Wine: the game code is the same there.
if(NOT CMAKE_CROSSCOMPILING)
    set(VISIT_MAPS_SAVES "${CMAKE_BINARY_DIR}/visit-maps-saves")
    set(VISIT_MAPS_CHECKPOINT "${CHECKPOINT_OUTPUT}/04-after-picking-starter")
    add_test(NAME visit-maps-clean-saves COMMAND ${CMAKE_COMMAND} -E rm -rf "${VISIT_MAPS_SAVES}")
    set_tests_properties(visit-maps-clean-saves PROPERTIES FIXTURES_SETUP visit-maps-clean)
    foreach(_groups 0-13 14-24 25-74)
        add_test(NAME visit-maps-save-${_groups}
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${VISIT_MAPS_CHECKPOINT}" "${VISIT_MAPS_SAVES}/${_groups}")
        set_tests_properties(visit-maps-save-${_groups} PROPERTIES
            FIXTURES_REQUIRED "played-checkpoints;visit-maps-clean"
            FIXTURES_SETUP visit-maps-save-${_groups})
        add_test(NAME visit-maps-${_groups}
            COMMAND pkmemerald --fast -s "${VISIT_MAPS_SAVES}/${_groups}" --visit-maps ${_groups})
        set_tests_properties(visit-maps-${_groups} PROPERTIES
            FIXTURES_REQUIRED visit-maps-save-${_groups}
            ENVIRONMENT "${HEADLESS_TEST_ENV};PKM_FIXED_TIME=2026-01-01 12:00:00;ASAN_OPTIONS=detect_leaks=0;UBSAN_OPTIONS=print_stacktrace=1")
    endforeach()
endif()

# Boots the platform and the game, runs 60 game frames, exits 0.
add_test(NAME pkmemerald-boot COMMAND pkmemerald -f 60 -s "${CMAKE_BINARY_DIR}/test-saves")
set_tests_properties(pkmemerald-boot PROPERTIES
    PASS_REGULAR_EXPRESSION "boot ok\nsave: [^\n]*\ngame init ok\naudio: [^\n]*\nran 60 frames"
    ENVIRONMENT "${HEADLESS_TEST_ENV}"
)

# Monkey runs (PLAN.md, Phase 19a): the game played with pseudo-random input
# (--monkey), from boot and after a quick start (Select on the title screen
# starts a new game in the truck). They fail on a crash, or, in a sanitizer
# build (PKM_SANITIZE), on any report. Each starts from an empty save
# directory, so earlier runs' saves can't change where it goes.
set(MONKEY_FRAMES 20000 CACHE STRING "Frames per monkey test run")
set(MONKEY_SAVES "${CMAKE_BINARY_DIR}/monkey-saves")
add_test(NAME monkey-clean-saves COMMAND ${CMAKE_COMMAND} -E rm -rf "${MONKEY_SAVES}")
set_tests_properties(monkey-clean-saves PROPERTIES FIXTURES_SETUP monkey-saves)
set(_monkey_env ${HEADLESS_TEST_ENV} "ASAN_OPTIONS=detect_leaks=0" "UBSAN_OPTIONS=print_stacktrace=1")
foreach(_seed 101 104)
    add_test(NAME monkey-boot-${_seed}
        COMMAND pkmemerald --fast -f ${MONKEY_FRAMES} -s "${MONKEY_SAVES}/boot-${_seed}" --monkey ${_seed})
    set_tests_properties(monkey-boot-${_seed} PROPERTIES
        FIXTURES_REQUIRED monkey-saves ENVIRONMENT "${_monkey_env}"
        PASS_REGULAR_EXPRESSION "ran ${MONKEY_FRAMES} frames")
endforeach()
foreach(_seed 1 2)
    add_test(NAME monkey-quickstart-${_seed}
        COMMAND pkmemerald --fast -f ${MONKEY_FRAMES} -s "${MONKEY_SAVES}/quickstart-${_seed}"
                -i "600:a,1300+10:select" --monkey ${_seed}@1900)
    set_tests_properties(monkey-quickstart-${_seed} PROPERTIES
        FIXTURES_REQUIRED monkey-saves ENVIRONMENT "${_monkey_env}"
        PASS_REGULAR_EXPRESSION "ran ${MONKEY_FRAMES} frames")
endforeach()

#-------------------------------------------------------------------------------
# Upstream's test suite (reference/test/; PLAN.md, Phase 19b)
#
# A second build of the game with TESTING=1, plus upstream's test runner and
# tests, as pkmemerald-tests (platform/upstream_tests/host_test_runner.c runs
# it). Linux only for now. PKM_UPSTREAM_TESTS and the test list are near
# the top (step 1 prepares the tests' assets).
#   cmake -S . -B build -DPKM_UPSTREAM_TESTS=ON
#   build/pkmemerald-tests [test/fpmath.c | name prefix | *infix]
#-------------------------------------------------------------------------------
if(PKM_UPSTREAM_TESTS)
    # test/test_runner_args.c and src/test_runner_stub.c hold the runner's
    # settings, which host_test_args.c provides instead.
    set(_game_sources ${REFERENCE_SRC})
    list(REMOVE_ITEM _game_sources src/test_runner_stub.c)

    pkm_preprocess(REFERENCE_TEST_PP "_test" 1 ${_game_sources} ${UPSTREAM_TEST_SOURCES})
    pkm_reference_objects(reference-test-objs ${REFERENCE_TEST_PP})
    # Event script macros that tests .include from inline asm (host
    # include/test/overworld_script.h), with host_assemble.sh's ARM-to-x86
    # fixups; searched before anything else by the assembler.
    set(HOST_TEST_ASM_DIR "${CMAKE_BINARY_DIR}/host_test_asm")
    set(_test_asm_includes
        constants/gba_constants.inc asm/macros/asm.inc asm/macros/event.inc
        data/script_cmd_table.inc data/specials.inc)
    set(_test_asm_outputs ${_test_asm_includes})
    list(TRANSFORM _test_asm_outputs PREPEND "${HOST_TEST_ASM_DIR}/")
    set(_test_asm_inputs ${_test_asm_includes})
    list(TRANSFORM _test_asm_inputs PREPEND "${REFERENCE_DIR}/")
    add_custom_command(
        OUTPUT ${_test_asm_outputs}
        COMMAND bash "${TOOLS_DIR}/host_assemble.sh" inc "${REFERENCE_DIR}" "${HOST_TEST_ASM_DIR}" ${_test_asm_includes}
        DEPENDS ${_test_asm_inputs} "${TOOLS_DIR}/host_assemble.sh"
        COMMENT "Preparing event script macros for the tests"
        VERBATIM
    )
    add_custom_target(host-test-asm DEPENDS ${_test_asm_outputs})
    add_dependencies(reference-test-objs host-test-asm)
    target_compile_options(reference-test-objs PRIVATE "-Wa,-I${HOST_TEST_ASM_DIR}")
    pkm_core_library(pkmemerald-test-core reference-test-objs)
    target_compile_definitions(pkmemerald-test-core PUBLIC TESTING=1)

    add_executable(pkmemerald-tests
        "${PLATFORM_DIR}/upstream_tests/host_test_runner.c"
        "${PLATFORM_DIR}/upstream_tests/host_test_args.c"
        "${PLATFORM_DIR}/upstream_tests/host_runner_selftest.c"
        "${PLATFORM_DIR}/upstream_tests/host_known_failures.c"
    )
    target_compile_options(pkmemerald-tests PRIVATE ${PLATFORM_WARNINGS})
    target_link_libraries(pkmemerald-tests PRIVATE
        -Wl,--whole-archive pkmemerald-test-core -Wl,--no-whole-archive
        m
    )
    target_link_options(pkmemerald-tests PRIVATE "-Wl,-T,${PLATFORM_DIR}/upstream_tests/tests.ld")
    set_property(TARGET pkmemerald-tests APPEND PROPERTY LINK_DEPENDS "${PLATFORM_DIR}/upstream_tests/tests.ld")

    # Without -j the runner starts a shard per CPU, while CTest counts a test
    # as one; these smaller tests run as one shard.
    # The non-battle tests (19b-2): one entry per file, upstream-<file>.
    set(_upstream_test_names)
    foreach(_src ${UPSTREAM_TEST_SOURCES})
        if(_src MATCHES "^test/(battle/|test_runner|test_test_runner)")
            continue()
        endif()
        get_filename_component(_name "${_src}" NAME_WE)
        add_test(NAME upstream-${_name} COMMAND pkmemerald-tests -j 1 "${_src}")
        list(APPEND _upstream_test_names upstream-${_name})
    endforeach()
    # The runner's own behaviour: a crashing test is reported and the run
    # goes on (upstream's own runner tests), and a failing test fails the run
    # with upstream's message.
    add_test(NAME upstream-runner-crash COMMAND pkmemerald-tests -j 1 "Tests resume after CRASH")
    add_test(NAME upstream-runner-fatalf COMMAND pkmemerald-tests -j 1 "fatalf counts as CRASH")
    add_test(NAME upstream-runner-player-name COMMAND pkmemerald-tests -j 1 "Tests initialize a terminated player name")
    add_test(NAME upstream-runner-detects-failure COMMAND pkmemerald-tests -j 1 "Host runner self-test")
    # Battles (19b-3, 19b-4): the framework's own tests, and the battle tests
    # built so far. The latter is split across all cores (-j), which CTest
    # reserves for it.
    cmake_host_system_information(RESULT _test_cores QUERY NUMBER_OF_LOGICAL_CORES)
    if(_test_cores GREATER 32)
        set(_test_cores 32)
    endif()
    add_test(NAME upstream-runner-battle COMMAND pkmemerald-tests -j 1 test/test_test_runner.c)
    add_test(NAME upstream-battle
        COMMAND pkmemerald-tests -j ${_test_cores} test/battle/)
    set_tests_properties(upstream-battle PROPERTIES PROCESSORS ${_test_cores})
    # The tests don't draw their frames (too slow), so this one does, to put
    # the renderer through every species' front animation (host_render.c).
    add_test(NAME upstream-runner-draw COMMAND pkmemerald-tests -j 1 --draw test/battle/front_anim.c)
    set_tests_properties(${_upstream_test_names} upstream-runner-crash upstream-runner-fatalf
                         upstream-runner-player-name upstream-runner-detects-failure
                         upstream-runner-battle upstream-battle upstream-runner-draw
        PROPERTIES ENVIRONMENT "${HEADLESS_TEST_ENV}")
    # Let the intentional SIGSEGV reach the parent instead of ASan converting
    # it to a normal exit(1), which cannot be resumed as a crashed test.
    if(PKM_SANITIZE MATCHES "address")
        set_property(TEST upstream-runner-crash APPEND PROPERTY ENVIRONMENT "ASAN_OPTIONS=handle_segv=0:detect_leaks=0")
    endif()
    set_tests_properties(upstream-runner-crash upstream-runner-fatalf PROPERTIES
        PASS_REGULAR_EXPRESSION "1 tests: 0 passed, 0 failed, 0 known failing, 1 expected failures")
    set_tests_properties(upstream-runner-detects-failure PROPERTIES
        PASS_REGULAR_EXPRESSION "a failing EXPECT_EQ fails: [^\n]*FAIL.*host_runner_selftest.c:[0-9]+: EXPECT_EQ\\(1, 2\\) failed.*1 tests: 0 passed, 1 failed")
endif()

# Check GBA bytecode/ROM data against upstream's ARM build. Native 64-bit
# metadata is additionally checked against source JSON/assembly and C offsets.
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
# Installed copies drop the debug info (~100 MB) but keep the symbol table,
# which crash reports name functions from; the build tree keeps everything.
install(CODE "
    execute_process(COMMAND \"${CMAKE_OBJCOPY}\" --strip-debug
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:pkmemerald>\"
        COMMAND_ERROR_IS_FATAL ANY)
")
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
