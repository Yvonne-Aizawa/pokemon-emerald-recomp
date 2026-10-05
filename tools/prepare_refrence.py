#!/usr/bin/env python3
"""
tools/prepare_refrence.py

Produce every *generated* input that the sources we build from `refrence/`
need: C files in `src/`, assembly data in `data/*.s`, and the songs in
`sound/songs/midi/`.

Upstream builds these as side effects of compiling the ROM. We don't want the
ROM (or the ARM toolchain), only the inputs, so we drive upstream's own make
rules in four steps:

  1. Build the host tools (preproc, gbagfx, mapjson, jsonproc, trainerproc, ...)
     via `make -f make_tools.mk`.
  2. `make generated` — auto-generated headers (map constants, trainers.h,
     wild_encounters.h, heal_locations.h, ...).
  3. Generated files wired up by explicit Makefile rules rather than scaninc:
     a few C headers, every map's header/events/connections .inc (mapjson),
     and each song's .s (mid2agb, from sound/songs/midi/*.mid).
  4. Ask make for the scaninc `.d` dependency files of every source we
     compile. These list INCBIN/INCGFX inputs and carry the recipes for
     `build/assets/**` (INCGFX) targets.
  5. Ask make for every non-source prerequisite found in those `.d` files:
     the converted graphics (.4bpp/.gbapal/.lz/.smol/...), sound samples
     (.bin) and generated data headers (teachable_learnsets.h, ...).

All outputs land where upstream puts them (refrence/build/ or next to their
sources) and are covered by upstream's .gitignore, so the submodule stays
clean in `git status`.

Usage: prepare_refrence.py REFRENCE_DIR SOURCE [SOURCE ...]
       (sources relative to REFRENCE_DIR: src/*.c or data/*.s)
"""

import glob
import os
import subprocess
import sys

OBJ_DIR = "build/emerald"  # upstream's $(OBJ_DIR) for the default target

EXTRA_GENERATED = [
    "src/data/pokemon/teachable_learnsets.h",  # pokemon.c
    "src/data/tutor_moves.h",                  # move_relearner.c
]


def make(refdir, *args):
    cmd = ["make", "-C", refdir, "--no-print-directory", f"-j{os.cpu_count() or 1}", *args]
    # Run as an independent make: when invoked from a CMake-generated Makefile
    # the parent's jobserver isn't shared with us, and inheriting its
    # MAKEFLAGS only produces "jobserver unavailable" warnings.
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}
    result = subprocess.run(cmd, stdout=subprocess.DEVNULL, env=env)
    if result.returncode != 0:
        sys.exit(f"prepare_refrence: `{' '.join(cmd)}` failed ({result.returncode})")


def dep_prereqs(dep_path):
    """Prerequisites of the first rule in a scaninc .d file."""
    with open(dep_path) as f:
        text = f.read().replace("\\\n", " ")
    first = text.split("\n", 1)[0]
    _, _, deps = first.partition(":")
    return deps.split()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    refdir = sys.argv[1]
    sources = sys.argv[2:]

    make(refdir, "-f", "make_tools.mk")
    make(refdir, "generated")
    # Generated files wired up by explicit Makefile rules, not by scaninc.
    maps = [
        os.path.join(os.path.dirname(j), inc)
        for j in sorted(glob.glob("data/maps/*/map.json", root_dir=refdir))
        for inc in ("header.inc", "events.inc", "connections.inc")
    ]
    songs = [m[:-len(".mid")] + ".s" for m in sorted(glob.glob("sound/songs/midi/*.mid", root_dir=refdir))]
    make(refdir, *EXTRA_GENERATED, *maps, *songs)

    dep_files = [f"{OBJ_DIR}/{os.path.splitext(s)[0]}.d" for s in sources]
    make(refdir, *dep_files)

    goals = set()
    for d in dep_files:
        for p in dep_prereqs(os.path.join(refdir, d)):
            # Headers and sources under version control need no rule; anything
            # else (graphics, generated .h tables, ...) is a build product.
            if p.endswith((".c", ".inc")) or (p.startswith("include/") and os.path.exists(os.path.join(refdir, p))):
                continue
            goals.add(p)

    if goals:
        make(refdir, *sorted(goals))


if __name__ == "__main__":
    main()
