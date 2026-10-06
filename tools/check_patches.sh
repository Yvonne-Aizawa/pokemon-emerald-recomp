#!/usr/bin/env bash
#
# tools/check_patches.sh
#
# Check that every platform/patches/*.patch still applies to reference/,
# exactly: run it after updating upstream (see CONTRIBUTING_PC.md). Changes
# nothing (patch --dry-run).
#
# A patch that FAILS must be redone against the new upstream file. One that
# applies only INEXACTLY (with an offset or fuzz) still builds -- the build
# applies it the same way -- but check that its hunks still land where they
# should, then regenerate it so it applies exactly again.
#
# Exit status: 1 if any patch fails, else 0.

set -uo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
failed=0 inexact=0 total=0

for patch_file in "$root"/platform/patches/*.patch; do
    name=${patch_file#"$root"/}
    total=$((total + 1))
    if ! output=$(patch --dry-run -p1 -d "$root/reference" < "$patch_file" 2>&1); then
        echo "FAILS:   $name"
        sed 's/^/         /' <<< "$output"
        failed=$((failed + 1))
    elif grep -qiE 'offset|fuzz' <<< "$output"; then
        echo "INEXACT: $name"
        grep -iE 'offset|fuzz' <<< "$output" | sed 's/^/         /'
        inexact=$((inexact + 1))
    fi
done

echo "$total patches: $((total - failed - inexact)) exact, $inexact inexact, $failed failing"
[[ $failed == 0 ]]
