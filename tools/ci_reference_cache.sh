#!/usr/bin/env bash
#
# tools/ci_reference_cache.sh
#
# CI only: save and restore what tools/prepare_reference.py generates in
# reference/ (upstream's host tools, converted graphics and sound, generated
# headers and map data -- every file git ignores there), so CI doesn't
# regenerate it on every run.
#
# The cache must never hold a built game (PLAN.md, Phase 17): it contains
# only what upstream's public sources generate, and `save` refuses to pack
# anything that looks like a build of the game (ROM, ELF, object files,
# executables, saves).
#
#   ci_reference_cache.sh save    ARCHIVE    pack the generated files
#   ci_reference_cache.sh restore ARCHIVE    unpack them
#
# Restored files all get one timestamp, newer than the fresh checkout, so
# upstream's make sees them as up to date (with their original timestamps,
# every checked-out source would look newer and make would redo it all).

set -euo pipefail

mode=$1 archive=$2
root=$(cd "$(dirname "$0")/.." && pwd)
reference=$root/reference

case $mode in
save)
    list=$(mktemp)
    trap 'rm -f "$list"' EXIT
    git -C "$reference" ls-files --others --ignored --exclude-standard > "$list"
    if forbidden=$(grep -E '\.(o|a|obj|elf|gba|sav|map|exe|dll)$|(^|/)pokeemerald[^/]*$' "$list"); then
        echo "ci_reference_cache.sh: refusing to cache build output:" >&2
        head -20 <<< "$forbidden" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$archive")"
    tar -C "$reference" -czf "$archive" --verbatim-files-from -T "$list"
    echo "cached $(wc -l < "$list") generated files ($(du -h "$archive" | cut -f1))"
    ;;
restore)
    now=$(date +%s)
    tar -C "$reference" -xzf "$archive"
    tar -tzf "$archive" | (cd "$reference" && xargs -d '\n' touch -h -d "@$now")
    echo "restored $(tar -tzf "$archive" | wc -l) generated files"
    ;;
*)
    echo "usage: $0 save|restore ARCHIVE" >&2
    exit 2
    ;;
esac
