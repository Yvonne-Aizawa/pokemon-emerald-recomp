#!/usr/bin/env bash
#
# tools/check_patches.sh
#
# Check that every platform/patches/**/*.patch still applies to reference/,
# exactly: run it after updating upstream (see CONTRIBUTING_PC.md). Changes
# nothing (patch --dry-run).
#
# A patch that FAILS must be redone against the new upstream file. One that
# applies only INEXACTLY (with an offset or fuzz) still builds -- the build
# applies it the same way -- but check that its hunks still land where they
# should, then regenerate it so it applies exactly again.
#
# A patch whose hunk line counts don't match its @@ headers also FAILS: patch
# stops reading a hunk once the header's counts are used up, so a hand-edited
# hunk can silently lose lines (the Moonlight hang in known_crashes.md).
# Regenerate it with diff -u (CLAUDE.md, "Making a patch").
#
# Exit status: 1 if any patch fails, else 0.

set -uo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
failed=0 inexact=0 total=0

# Print one line per hunk whose body doesn't match its @@ header's counts.
check_hunk_counts() {
    awk '
        function check() {
            if (inhunk && (old != want_old || new != want_new))
                printf "line %d: header says -%d +%d, hunk has -%d +%d\n",
                       start, want_old, want_new, old, new
            inhunk = 0
        }
        /^@@ / {
            check()
            if (!match($0, /^@@ -[0-9]+(,[0-9]+)? \+[0-9]+(,[0-9]+)? @@/))
                next
            split(substr($0, 4, RLENGTH - 6), r, " ")
            want_old = split(r[1], o, ",") > 1 ? o[2] : 1
            want_new = split(r[2], n, ",") > 1 ? n[2] : 1
            old = new = 0; start = NR; inhunk = 1
            next
        }
        /^(--- |\+\+\+ |diff )/ && !(inhunk && (old < want_old || new < want_new)) {
            check(); next
        }
        !inhunk { next }
        /^\\/ { next }
        /^-/ { old++; next }
        /^\+/ { new++; next }
        { old++; new++ }   # context: " " or an empty line
        END { check() }
    ' "$1"
}

while IFS= read -r patch_file; do
    name=${patch_file#"$root"/}
    total=$((total + 1))
    if counts=$(check_hunk_counts "$patch_file") && [[ -n $counts ]]; then
        echo "FAILS:   $name (hunk line counts don't match; regenerate it with diff -u)"
        sed 's/^/         /' <<< "$counts"
        failed=$((failed + 1))
    elif ! output=$(patch --dry-run -p1 -d "$root/reference" < "$patch_file" 2>&1); then
        echo "FAILS:   $name"
        sed 's/^/         /' <<< "$output"
        failed=$((failed + 1))
    elif grep -qiE 'offset|fuzz' <<< "$output"; then
        echo "INEXACT: $name"
        grep -iE 'offset|fuzz' <<< "$output" | sed 's/^/         /'
        inexact=$((inexact + 1))
    fi
done < <(find "$root/platform/patches" -name '*.patch' | sort)

echo "$total patches: $((total - failed - inexact)) exact, $inexact inexact, $failed failing"
[[ $failed == 0 ]]
