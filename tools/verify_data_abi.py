#!/usr/bin/env python3
"""
tools/verify_data_abi.py

Check that the host build reads upstream's assembly data exactly as the GBA
build does. Needs the ARM toolchain (arm-none-eabi-*) and gdb.

1. Data: every object tools/host_assemble.sh produced (data/*.s and songs)
   is reassembled with upstream's real ARM pipeline and flags, and every
   data section must match byte for byte, with relocations at the same
   offsets against the same symbols.

2. Layout: the C structs the game overlays on that data (map headers,
   events, connections, voicegroups, ...) must have the same size and member
   offsets under the GBA ABI (arm-none-eabi-gcc -mabi=apcs-gnu) as in the
   host build. Compared via `gdb ptype /o` on a probe compiled both ways.

Usage: verify_data_abi.py REFERENCE_DIR BUILD_DIR HOST_CC [HOST_CFLAGS...]
"""

import os
import subprocess
import sys
import tempfile

# Upstream reference/Makefile: ASFLAGS and CPPFLAGS for data/*.s, and the C
# flags that determine struct layout.
ARM_ASFLAGS = ["-mcpu=arm7tdmi", "-march=armv4t", "-meabi=5", "--defsym", "MODERN=1", "--defsym", "EMERALD=1"]
ARM_CPPFLAGS = ["-iquote", "include", "-I", "include", "-Wno-trigraphs", "-DMODERN=1", "-DTESTING=0", "-DEMERALD", "-std=gnu17"]
ARM_CFLAGS = ["-mthumb", "-mthumb-interwork", "-mabi=apcs-gnu", "-mtune=arm7tdmi", "-march=armv4t", "-std=gnu17"]
LAYOUT_DEFINES = ["-DMODERN=1", "-DTESTING=0", "-DEMERALD"]

# Structs that C code reads directly out of the assembled data.
LAYOUT_STRUCTS = [
    "MapHeader", "MapLayout", "MapEvents", "ObjectEventTemplate", "WarpEvent",
    "CoordEvent", "BgEvent", "MapConnections", "MapConnection", "Tileset", "ToneData",
]


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, **kw)


def data_sections(path, objdump):
    """{name: size} of allocated sections that hold data (or are bss)."""
    lines = run([objdump, "-h", path], text=True).stdout.splitlines()
    out = {}
    for i, line in enumerate(lines):
        f = line.split()
        if len(f) < 7 or not f[0].isdigit():
            continue
        name, size, flags = f[1], int(f[2], 16), lines[i + 1]
        if "ALLOC" not in flags or "CODE" in flags:
            continue
        if "CONTENTS" in flags or size:
            out[name] = (size, "CONTENTS" in flags)
    return out


def section_bytes(path, section, objcopy, tmp):
    dest = os.path.join(tmp, "section.bin")
    run([objcopy, "-O", "binary", f"--only-section={section}", path, dest])
    with open(dest, "rb") as f:
        return f.read()


def relocations(path, objdump):
    """Sorted (offset, target) pairs; relocation *types* differ by arch."""
    out = []
    section = None
    for line in run([objdump, "-r", path], text=True).stdout.splitlines():
        if line.startswith("RELOCATION RECORDS FOR"):
            section = line.split("[")[1].split("]")[0]
            continue
        f = line.split()
        if len(f) == 3 and all(c in "0123456789abcdef" for c in f[0]):
            out.append((section, int(f[0], 16), f[2]))
    return sorted(out)


def compare_object(arm, host, tmp):
    sa = data_sections(arm, "arm-none-eabi-objdump")
    sh = data_sections(host, "objdump")
    if sa != sh:
        return f"sections differ: arm={sa} host={sh}"
    for name, (size, has_contents) in sa.items():
        if has_contents and section_bytes(arm, name, "arm-none-eabi-objcopy", tmp) != section_bytes(host, name, "objcopy", tmp):
            return f"section {name} contents differ"
    if relocations(arm, "arm-none-eabi-objdump") != relocations(host, "objdump"):
        return "relocations differ"
    return None


def check_data(refdir, asm_dir, tmp):
    jobs = []
    for name in sorted(os.listdir(os.path.join(refdir, "data"))):
        if name.endswith(".s"):
            jobs.append(("data", f"data/{name}", os.path.join(asm_dir, "data", name[:-2] + ".o")))
    midi = os.path.join(refdir, "sound/songs/midi")
    for name in sorted(os.listdir(midi)):
        if name.endswith(".mid"):
            jobs.append(("song", f"sound/songs/midi/{name[:-4]}.s", os.path.join(asm_dir, "songs", name[:-4] + ".o")))

    failures = 0
    for kind, src, host_obj in jobs:
        arm_obj = os.path.join(tmp, "arm.o")
        if kind == "data":
            pipeline = (
                f"tools/preproc/preproc {src} charmap.txt"
                f" | arm-none-eabi-cpp {' '.join(ARM_CPPFLAGS)} -"
                f" | tools/preproc/preproc -ie {src} charmap.txt"
                f" | arm-none-eabi-as {' '.join(ARM_ASFLAGS)} -o {arm_obj}"
            )
            run(["bash", "-o", "pipefail", "-c", pipeline], cwd=refdir)
        else:
            run(["arm-none-eabi-as", *ARM_ASFLAGS, "-I", "sound", "-o", arm_obj, src], cwd=refdir)
        problem = compare_object(arm_obj, host_obj, tmp)
        if problem:
            print(f"FAIL {src}: {problem}")
            failures += 1
    print(f"data: {len(jobs)} objects compared against the ARM build, {failures} mismatched")
    return failures


def check_layout(refdir, build_dir, host_cc, host_cflags, tmp):
    probe = os.path.join(tmp, "probe.c")
    with open(probe, "w") as f:
        f.write('#include "global.h"\n#include "fieldmap.h"\n#include "m4a.h"\n')
        for s in LAYOUT_STRUCTS:
            f.write(f"struct {s} gProbe_{s};\n")

    arm_o, host_o = os.path.join(tmp, "probe_arm.o"), os.path.join(tmp, "probe_host.o")
    run(["arm-none-eabi-gcc", *ARM_CFLAGS, *LAYOUT_DEFINES, "-g", "-w", "-c",
         "-iquote", "include", "-iquote", "src", probe, "-o", arm_o], cwd=refdir)
    run([host_cc, *host_cflags, *LAYOUT_DEFINES, "-DHOST_BUILD=1", "-std=gnu17", "-g", "-w", "-c",
         "-iquote", os.path.join(build_dir, "host_include"), "-iquote", "src", probe, "-o", host_o], cwd=refdir)

    gdb_args = [a for s in LAYOUT_STRUCTS for a in ("-ex", f"ptype /o struct {s}")]
    arm = run(["gdb", "-batch", *gdb_args, arm_o], text=True).stdout
    host = run(["gdb", "-batch", *gdb_args, host_o], text=True).stdout
    if arm != host:
        print("FAIL struct layouts differ between the GBA ABI and the host build:")
        for a, h in zip(arm.splitlines(), host.splitlines()):
            if a != h:
                print(f"  arm:  {a.strip()}\n  host: {h.strip()}")
        return 1
    print(f"layout: {len(LAYOUT_STRUCTS)} structs identical under both ABIs")
    return 0


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    refdir, build_dir, host_cc, host_cflags = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    with tempfile.TemporaryDirectory() as tmp:
        failures = check_data(refdir, os.path.join(build_dir, "reference_asm"), tmp)
        failures += check_layout(refdir, build_dir, host_cc, host_cflags, tmp)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
