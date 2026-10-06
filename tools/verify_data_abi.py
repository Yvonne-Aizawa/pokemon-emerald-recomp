#!/usr/bin/env python3
"""
tools/verify_data_abi.py

Check that the host build reads upstream's assembly data exactly as the GBA
build does. Needs the ARM toolchain (arm-none-eabi-*).

1. Data: every object tools/host_assemble.sh produced (data/*.s and songs)
   is reassembled with upstream's real ARM pipeline and flags, and every
   data section must match byte for byte, with relocations at the same
   offsets against the same symbols.

2. Layout: the C structs the game overlays on that data (map headers,
   events, connections, voicegroups, ...) must have the same size and member
   offsets (bitfields included) under the GBA ABI (arm-none-eabi-gcc
   -mabi=apcs-gnu) as in the host build. Compared from the DWARF debug info
   of a probe compiled both ways.

Windows (PE/COFF) host objects are compared too: COFF has no empty .data,
pads section sizes to their alignment (with zeros) and prefixes C symbol
names with `_`; those differences are allowed for.

Usage: verify_data_abi.py REFERENCE_DIR BUILD_DIR HOST_CC [HOST_CFLAGS...]
The host binutils are HOST_OBJDUMP and HOST_OBJCOPY from the environment
(default: objdump, objcopy).
"""

import os
import re
import subprocess
import sys
import tempfile

HOST_OBJDUMP = os.environ.get("HOST_OBJDUMP") or "objdump"
HOST_OBJCOPY = os.environ.get("HOST_OBJCOPY") or "objcopy"

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
        if size:
            out[name] = (size, "CONTENTS" in flags)
    return out


def is_coff(path, objdump):
    return "file format pe-" in run([objdump, "-f", path], text=True).stdout


def section_bytes(path, section, objcopy, tmp):
    dest = os.path.join(tmp, "section.bin")
    run([objcopy, "-O", "binary", f"--only-section={section}", path, dest])
    with open(dest, "rb") as f:
        return f.read()


def section_names(path, objdump):
    """All section names, in section-number order."""
    names = []
    for line in run([objdump, "-h", path], text=True).stdout.splitlines():
        f = line.split()
        if len(f) >= 7 and f[0].isdigit():
            names.append(f[1])
    return names


def defined_symbols(path, objdump, coff, sections):
    """{name: (section, value)} of the symbols defined in a section."""
    out = {}
    for line in run([objdump, "-t", path], text=True).stdout.splitlines():
        if coff:
            m = re.match(r"\[\s*\d+\]\(sec\s+(-?\d+)\).* 0x([0-9a-f]+) (\S+)$", line)
            if m and 0 < int(m.group(1)) <= len(sections):
                name = m.group(3)[1:] if m.group(3).startswith("_") else m.group(3)
                out.setdefault(name, (sections[int(m.group(1)) - 1], int(m.group(2), 16)))
        else:
            m = re.match(r"([0-9a-f]+) .{7} (\S+)\s+[0-9a-f]+\s+(\S+)$", line)
            if m and m.group(2) in sections:
                out.setdefault(m.group(3), (m.group(2), int(m.group(1), 16)))
    return out


def relocations(path, objdump, objcopy, tmp, coff=False):
    """{(section, offset): target} for every 32-bit relocation, with the
    target resolved to what it points at: (section, offset) for data in this
    object -- whether the assembler referenced it through its symbol (ELF) or
    through the section plus an offset stored in place (COFF) -- or
    ("extern", symbol, addend). Relocation *types* differ by arch."""
    sections = section_names(path, objdump)
    symbols = defined_symbols(path, objdump, coff, sections)
    contents = {}
    out = {}
    section = None
    for line in run([objdump, "-r", path], text=True).stdout.splitlines():
        if line.startswith("RELOCATION RECORDS FOR"):
            section = line.split("[")[1].split("]")[0]
            continue
        f = line.split()
        if len(f) != 3 or not all(c in "0123456789abcdef" for c in f[0]):
            continue
        offset, target = int(f[0], 16), f[2]
        if section not in contents:
            contents[section] = section_bytes(path, section, objcopy, tmp)
        addend = int.from_bytes(contents[section][offset:offset + 4], "little")
        if coff and target.startswith("_"):
            target = target[1:]
        if target in sections:
            resolved = (target, addend)
        elif target in symbols:
            resolved = (symbols[target][0], symbols[target][1] + addend)
        else:
            resolved = ("extern", target, addend)
        out[(section, offset)] = resolved
    return out


def masked(data, section, relocs):
    """`data` with the relocated words zeroed (their meaning is in relocs)."""
    data = bytearray(data)
    for sec, offset in relocs:
        if sec == section:
            data[offset:offset + 4] = bytes(4)
    return bytes(data)


def compare_object(arm, host, tmp):
    coff = is_coff(host, HOST_OBJDUMP)
    sa = data_sections(arm, "arm-none-eabi-objdump")
    sh = data_sections(host, HOST_OBJDUMP)
    if sa.keys() != sh.keys():
        return f"sections differ: arm={sa} host={sh}"
    ra = relocations(arm, "arm-none-eabi-objdump", "arm-none-eabi-objcopy", tmp)
    rh = relocations(host, HOST_OBJDUMP, HOST_OBJCOPY, tmp, coff=coff)
    if ra != rh:
        diff = sorted(set(ra.items()) ^ set(rh.items()))[:3]
        return f"relocations differ, e.g. {diff}"
    for name, (size, has_contents) in sa.items():
        host_size, host_contents = sh[name]
        # COFF rounds section sizes up to the section's alignment.
        padding = host_size - size
        if host_contents != has_contents or padding < 0 or padding > (15 if coff else 0):
            return f"section {name} differs: arm={sa[name]} host={sh[name]}"
        if has_contents:
            arm_bytes = masked(section_bytes(arm, name, "arm-none-eabi-objcopy", tmp), name, ra)
            host_bytes = masked(section_bytes(host, name, HOST_OBJCOPY, tmp), name, rh)
            if host_bytes[:size] != arm_bytes or host_bytes[size:].strip(b"\0"):
                return f"section {name} contents differ"
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


def struct_layouts(path, objdump):
    """{struct: (size, [(member, bit offset, bit size or None)])} for the
    LAYOUT_STRUCTS defined at the top level of `path`'s DWARF info."""
    structs = []
    current = member = None
    for line in run([objdump, "--dwarf=info", path], text=True).stdout.splitlines():
        m = re.match(r"\s*<(\d+)><[0-9a-f]+>: Abbrev Number: \d+ \((\w+)\)", line)
        if m:
            depth, tag = int(m.group(1)), m.group(2)
            if depth == 1:
                current = {"name": None, "size": None, "members": []}
                if tag == "DW_TAG_structure_type":
                    structs.append(current)
                member = None
            elif depth == 2 and current is not None and tag == "DW_TAG_member":
                member = {"name": None, "byte": None, "bit": None, "bits": None}
                current["members"].append(member)
            else:
                member = None
            continue
        m = re.match(r"\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$", line)
        if not m or current is None:
            continue
        attr, value = m.group(1), m.group(2).strip()
        if attr == "DW_AT_name":
            if "indirect" in value:
                value = value.rsplit(": ", 1)[1]
            (member if member is not None else current)["name"] = value
        elif attr == "DW_AT_byte_size" and member is None:
            current["size"] = int(value, 0)
        elif attr == "DW_AT_data_member_location" and member is not None:
            member["byte"] = int(value.split()[0], 0)
        elif attr == "DW_AT_data_bit_offset" and member is not None:
            member["bit"] = int(value, 0)
        elif attr == "DW_AT_bit_size" and member is not None:
            member["bits"] = int(value, 0)

    out = {}
    for s in structs:
        if s["name"] in LAYOUT_STRUCTS and s["size"] is not None and s["name"] not in out:
            out[s["name"]] = (s["size"], [
                (m["name"], m["bit"] if m["bit"] is not None else (m["byte"] or 0) * 8, m["bits"])
                for m in s["members"]
            ])
    return out


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

    arm = struct_layouts(arm_o, "arm-none-eabi-objdump")
    host = struct_layouts(host_o, HOST_OBJDUMP)
    if sorted(arm) != sorted(LAYOUT_STRUCTS):
        print(f"FAIL could not read the layouts of {sorted(set(LAYOUT_STRUCTS) - set(arm))} from the ARM probe")
        return 1
    if arm != host:
        print("FAIL struct layouts differ between the GBA ABI and the host build:")
        for name in LAYOUT_STRUCTS:
            if arm[name] != host.get(name):
                print(f"  struct {name}:\n    arm:  {arm[name]}\n    host: {host.get(name)}")
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
