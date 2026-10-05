#!/usr/bin/env bash
#
# tools/host_preprocess.sh
#
# Host equivalent of upstream's per-file C pipeline (refrence/Makefile):
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
# flags GCC appends. On x86 the comment character is `#`.
#
# Usage:
#   host_preprocess.sh REFDIR PREPROC OUT_I DEPFILE SRC_NAME INPUT -- CC CPPFLAGS...
#
#   REFDIR    refrence/ (cwd for preproc; INCBIN paths are relative to it)
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

"$@" -E -MD -MF "$depfile" -MT "$out" "$input" \
    | "$preproc" -i -g build/assets "$src_name" charmap.txt \
    | sed -E 's/(\\"aM\\",%progbits,[0-9]+) @"/\1 #"/g' \
    > "$out.tmp.$$"

# Per-process temp name: two builds racing in one build tree can't clobber
# each other's half-written output.
mv "$out.tmp.$$" "$out"
