#!/usr/bin/env bash
#
# tools/host_preprocess.sh
#
# Host equivalent of upstream's per-file C pipeline (reference/Makefile):
#
#     cpp $(CPPFLAGS) src/x.c | preproc -i -g build/assets src/x.c charmap.txt | cc1 ...
#
# We stop after preproc and write a `.i` file; CMake then compiles that with the
# host compiler. preproc is what turns `_("TEXT")` / COMPOUND_STRING into
# charmap-encoded byte arrays and INCBIN/INCGFX into literal data, so it can't
# be skipped.
#
# One x86 fixup is applied to preproc's output: COMPOUND_STRING emits
#     section(".rodata.compound_string.N,\"aM\",%progbits,SIZE @")
# relying on `@` being the ARM assembler's comment character to swallow the
# flags GCC appends. On x86 the comment character is `#`. Windows objects
# (PE/COFF) take no ELF section flags, and every distinct section would become
# a 4 KiB-aligned section of the executable (thousands, more than Windows
# loads), so there all of them go into .rdata$compound_string, which the
# linker merges into .rdata. Identical strings are then not merged; nothing
# depends on that.
#
# Usage:
#   host_preprocess.sh REFDIR PREPROC OUT_I DEPFILE SRC_NAME INPUT -- CC CPPFLAGS...
#
#   REFDIR    reference/ (cwd for preproc; INCBIN paths are relative to it)
#   PREPROC   path to the built preproc tool
#   OUT_I     output .i file
#   DEPFILE   Makefile-style depfile for CMake
#   SRC_NAME  source path as upstream names it (src/x.c), for preproc
#   INPUT     file actually preprocessed (src/x.c or a patched copy)
#   CC ...    host compiler followed by preprocessor flags

set -euo pipefail

refdir=$1 preproc=$2 out=$3 depfile=$4 src_name=$5 input=$6
shift 6
[[ $1 == -- ]] && shift

cd "$refdir"

# (Not `| grep -q`: grep exiting early can kill cc with SIGPIPE, which
# pipefail turns into a false result.)
if [[ $("$@" -dM -E -x c /dev/null) == *"#define _WIN32 "* ]]; then
    section_fixup='s/section\("\.rodata\.compound_string\.[^",\\]*,\\"aM\\",%progbits,[0-9]+ @"/section(".rdata$compound_string"/g'
else
    section_fixup='s/(\\"aM\\",%progbits,[0-9]+) @"/\1 #"/g'
fi

source_fixup=''
if [[ $src_name == src/pokemon.c ]]; then
    # Some monster palettes contain fewer colors, but every sprite palette
    # load copies 16. Explicit array bounds zero-fill the missing colors.
    source_fixup='s/(const u16 gMon(Shiny)?Palette_[[:alnum:]_]+)\[\]/\1[16]/g'
fi

"$@" -E -MD -MF "$depfile" -MT "$out" "$input" \
    | sed -E "$source_fixup" \
    | "$preproc" -i -g build/assets "$src_name" charmap.txt \
    | sed -E "$section_fixup" \
    > "$out.tmp.$$"

# Per-process temp name: two builds racing in one build tree can't clobber
# each other's half-written output.
mv "$out.tmp.$$" "$out"
