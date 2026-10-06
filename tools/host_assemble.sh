#!/usr/bin/env bash
#
# tools/host_assemble.sh
#
# Assemble upstream's assembly *data* (no ARM code) with the host assembler.
# These files are byte/pointer tables -- event and battle scripts, maps,
# voicegroups, songs. They need three ARM-vs-x86 fixups, applied to the
# assembler's input (see arm_to_host below):
#
#   `@` comments   ARM's line-comment character; x86 uses `#`. (`\@` is kept.)
#   .word          4 bytes on ARM, 2 on x86 -> .4byte.
#   .align N       2^N bytes on ARM, N bytes on x86 -> .p2align N.
#
# preproc's first pass inlines every .include, so for data files the whole
# input passes through the fixups.
#
#   data:  Same pipeline as upstream (reference/Makefile, data/*.s):
#              preproc SRC charmap.txt | cpp | preproc -ie SRC charmap.txt | as
#          preproc expands strings and .include/INCBIN; cpp the #includes.
#
#   song:  mid2agb output (sound/songs/midi/*.s), which upstream feeds
#          straight to `as -I sound`. The shared sound/MPlayDef.s it
#          .includes (only .equ definitions) gets the same fixups once, into
#          HOST_SOUND_DIR, which is searched first.
#
# Usage:
#   host_assemble.sh data REFDIR PREPROC OUT.o DEPFILE SRC -- CC CPPFLAGS... -- CC ASFLAGS...
#   host_assemble.sh song REFDIR HOST_SOUND_DIR OUT.o SRC -- CC ASFLAGS...
#
#   SRC is relative to REFDIR (the working directory, as upstream builds).
#   CC is the host C compiler driver, used both as the preprocessor and as the
#   assembler front end (`CC -c -x assembler -`), so -m32 etc. carry over.
#
# Windows (PE/COFF) targets, detected from CC, need two more fixups:
#   ELF directives  `.type`/`.size` are dropped (symbol metadata only);
#                   `.section NAME, "aw", %progbits` flags become COFF's.
#   Symbol names    32-bit Windows C names carry a leading underscore, so every
#                   global symbol the object defines or references is renamed
#                   with HOST_OBJCOPY/HOST_NM (environment), after assembling.

set -euo pipefail

# `@` starts a comment, except in `\@` (the macro invocation counter, used
# for local labels). preproc has already turned strings into .byte lists, so
# no `@` hides inside a string.
arm_to_host() {
    local coff=()
    if [[ $windows == 1 ]]; then
        coff=(
            -e '/^[[:space:]]*\.(type|size)[[:space:]]/d'
            -e 's/^([[:space:]]*\.section[[:space:]]+[^,]+),[[:space:]]*"aw",[[:space:]]*%progbits/\1, "dw"/'
            -e 's/^([[:space:]]*\.section[[:space:]]+[^,]+),[[:space:]]*"a",[[:space:]]*%progbits/\1, "dr"/'
        )
    fi
    sed -E \
        -e 's/(^|[^\\])@.*$/\1/' \
        -e 's/(^|[[:space:]:;])\.word([[:space:]])/\1.4byte\2/' \
        -e 's/(^|[[:space:]:;])\.align([[:space:]])/\1.p2align\2/' \
        "${coff[@]}" \
        "$@"
}

# Windows: prefix every global symbol of OUT (defined or referenced) with `_`.
add_symbol_underscores() {
    local out=$1
    "${HOST_NM:?}" -g "$out" | awk 'NF { print $NF " _" $NF }' > "$out.syms"
    "${HOST_OBJCOPY:?}" --redefine-syms="$out.syms" "$out"
    rm -f "$out.syms"
}

# The CC after the last `--` decides the target.
target_is_windows() {
    [[ $("$@" -dM -E -x c /dev/null) == *"#define _WIN32 "* ]]
}

mode=$1
shift
windows=0

case $mode in
data)
    refdir=$1 preproc=$2 out=$3 depfile=$4 src=$5
    shift 5
    [[ $1 == -- ]] && shift
    cpp_cmd=()
    while [[ $1 != -- ]]; do cpp_cmd+=("$1"); shift; done
    shift
    target_is_windows "$@" && windows=1
    cd "$refdir"
    "$preproc" "$src" charmap.txt \
        | "${cpp_cmd[@]}" -E -x assembler-with-cpp -MD -MF "$depfile" -MT "$out" - \
        | "$preproc" -ie "$src" charmap.txt \
        | arm_to_host \
        | "$@" -c -x assembler -o "$out" -
    if [[ $windows == 1 ]]; then add_symbol_underscores "$out"; fi
    ;;
song)
    refdir=$1 host_sound=$2 out=$3 src=$4
    shift 4
    [[ $1 == -- ]] && shift
    target_is_windows "$@" && windows=1
    cd "$refdir"
    if [[ ! -f $host_sound/MPlayDef.s || sound/MPlayDef.s -nt $host_sound/MPlayDef.s ]]; then
        mkdir -p "$host_sound"
        arm_to_host sound/MPlayDef.s > "$host_sound/MPlayDef.s.tmp.$$"
        mv "$host_sound/MPlayDef.s.tmp.$$" "$host_sound/MPlayDef.s"
    fi
    arm_to_host "$src" \
        | "$@" -c -x assembler "-Wa,-I$host_sound" -Wa,-Isound -o "$out" -
    if [[ $windows == 1 ]]; then add_symbol_underscores "$out"; fi
    ;;
*)
    echo "host_assemble.sh: unknown mode '$mode'" >&2
    exit 2
    ;;
esac
