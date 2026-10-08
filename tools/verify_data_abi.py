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

On 64-bit Linux, native metadata deliberately changes pointer widths. The
canonical 32-bit adapter is checked against ARM; the actual adapted bytecode
and ROM voice bytes are also compared through shared labels and relocations.
Native C offsets are checked against explicit schemas, and linked map, song,
and function-pointer metadata is verified against upstream JSON/assembly.

Windows (PE/COFF) host objects are compared too: COFF has no empty .data,
pads section sizes to their alignment (with zeros) and prefixes C symbol
names with `_`; those differences are allowed for.

Usage: verify_data_abi.py REFERENCE_DIR BUILD_DIR HOST_CC [HOST_CFLAGS...]
The host binutils are HOST_OBJDUMP and HOST_OBJCOPY from the environment
(default: objdump, objcopy).
"""

import os
import bisect
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
    """{(section, offset): target} for every data relocation, with the
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
        width = 2 if f[1].endswith("16") else (1 if f[1].endswith("8") else (8 if f[1] == "R_X86_64_64" else 4))
        if section not in contents:
            contents[section] = section_bytes(path, section, objcopy, tmp)
        if f[1].startswith("R_X86_64_"):
            # ELF64 uses RELA: the addend is printed with the symbol, not
            # stored in the bytes at the relocation site as on ARM/i386.
            match = re.fullmatch(r"(.+?)([+-]0x[0-9a-f]+)?", target)
            target = match[1]
            addend = int(match[2], 16) if match[2] else 0
        else:
            addend = int.from_bytes(contents[section][offset:offset + width], "little")
        if coff and target.startswith("_"):
            target = target[1:]
        if target in sections:
            resolved = (target, addend)
        elif target in symbols:
            resolved = (symbols[target][0], symbols[target][1] + addend)
        else:
            resolved = ("extern", target, addend)
        out[(section, offset)] = (*resolved, width)
    return out


def masked(data, section, relocs):
    """`data` with the relocated fields zeroed (their meaning is in relocs)."""
    data = bytearray(data)
    for (sec, offset), target in relocs.items():
        if sec == section:
            width = target[-1]
            data[offset:offset + width] = bytes(width)
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


def compare_native_bytecode(arm, host, kind, src, tmp):
    """Compare actual adapted bytecode/voice bytes, allowing metadata growth.

    Local symbol addresses map shifted sections back to their ARM positions;
    widened native tables are independently checked against source data.
    """
    if src in ("data/maps.s", "data/map_events.s"):
        return None
    arm_sections = section_names(arm, "arm-none-eabi-objdump")
    host_sections = section_names(host, HOST_OBJDUMP)
    a_symbols = defined_symbols(arm, "arm-none-eabi-objdump", False, arm_sections)
    h_symbols = defined_symbols(host, HOST_OBJDUMP, False, host_sections)
    if "gMPlayTableGba" in h_symbols:
        h_symbols["gMPlayTable"] = h_symbols["gMPlayTableGba"]
    ra = relocations(arm, "arm-none-eabi-objdump", "arm-none-eabi-objcopy", tmp)
    rh = relocations(host, HOST_OBJDUMP, HOST_OBJCOPY, tmp)
    skipped = {"gSongTable", "gScriptCmdTable", "gSpecials", "gSpecialVars", "gStdScripts", "gFieldEffectScriptPointers"}
    if kind == "song":
        skipped.add(os.path.basename(src)[:-2])
    maps = {}
    for name, (section, original) in a_symbols.items():
        if name in h_symbols and h_symbols[name][0] == section:
            maps.setdefault(section, []).append((h_symbols[name][1], original))
    maps = {section: sorted(anchors) for section, anchors in maps.items()}
    a_offsets = {section: sorted(off for sec, off in ra if sec == section) for section in arm_sections}
    h_offsets = {section: sorted(off for sec, off in rh if sec == section) for section in host_sections}
    section_sizes = data_sections(arm, "arm-none-eabi-objdump")
    for section, (size, has_contents) in section_sizes.items():
        if not has_contents:
            continue
        anchors = {}
        for name, (sec, offset) in a_symbols.items():
            if sec == section and name in h_symbols and h_symbols[name][0] == section:
                anchors.setdefault(offset, []).append(name)
        points = sorted(anchors)
        if not points:
            continue
        ab = section_bytes(arm, section, "arm-none-eabi-objcopy", tmp)
        hb = section_bytes(host, section, HOST_OBJCOPY, tmp)
        for i, offset in enumerate(points):
            names = anchors[offset]
            if skipped.intersection(names):
                continue
            end = points[i + 1] if i + 1 < len(points) else size
            ho = h_symbols[names[0]][1]
            length = end - offset
            ao, hofs = a_offsets[section], h_offsets.get(section, [])
            ar = {(section, off - offset): ra[(section, off)] for off in ao[bisect.bisect_left(ao, offset):bisect.bisect_left(ao, end)]}
            hr = {(section, off - ho): rh[(section, off)] for off in hofs[bisect.bisect_left(hofs, ho):bisect.bisect_left(hofs, ho + length)]}
            # Resolve host targets through the nearest shared local label.
            normalized = {}
            for site, target in hr.items():
                if target[0] in host_sections:
                    candidates = maps.get(target[0], [])
                    index = bisect.bisect_right(candidates, (target[1], float('inf'))) - 1
                    if index >= 0:
                        base, original = candidates[index]
                        target = (target[0], original + target[1] - base, target[-1])
                normalized[site] = target
            if ar != normalized:
                return f"actual native bytecode relocations differ at {names[0]}"
            if masked(ab[offset:end], section, ar) != masked(hb[ho:ho + length], section, hr):
                return f"actual native bytecode/voice bytes differ at {names[0]}"
    return None


def check_data(refdir, asm_dir, tmp, host_cc=None):
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
        actual_host_obj = host_obj
        adapted = host_cc and (kind == "song" or src in (
                "data/maps.s", "data/map_events.s", "data/sound_data.s",
                "data/event_scripts.s", "data/field_effect_scripts.s"))
        if adapted:
            # These files contain deliberately native metadata. Check their
            # original bytecode encoding through the 32-bit adapter; the actual
            # linked 64-bit metadata is independently checked against source
            # JSON/assembly by verify_native_data.py below.
            host_obj = os.path.join(tmp, "canonical.o")
            adapter = os.path.join(os.path.dirname(__file__), "host_assemble.sh")
            if kind == "data":
                run(["bash", adapter, "data", refdir, os.path.join(refdir, "tools/preproc/preproc"),
                     host_obj, os.path.join(tmp, "canonical.d"), src, "--", host_cc,
                     "-m32", *ARM_CPPFLAGS, "--", host_cc, "-m32"])
            else:
                run(["bash", adapter, "song", refdir, os.path.join(tmp, "sound"),
                     host_obj, src, "--", host_cc, "-m32"])
        problem = compare_object(arm_obj, host_obj, tmp)
        if not problem and adapted:
            problem = compare_native_bytecode(arm_obj, actual_host_obj, kind, src, tmp)
        if problem:
            print(f"FAIL {src}: {problem}")
            failures += 1
    print(f"data: {len(jobs)} objects compared against the ARM build, {failures} mismatched")
    return failures


def struct_layouts(path, objdump, names=LAYOUT_STRUCTS):
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
        if s["name"] in names and s["size"] is not None and s["name"] not in out:
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


def check_native_layout(refdir, build_dir, host_cc, host_cflags, tmp):
    # Offsets independently specified by the linked-data verifier, in bytes.
    # Bitfield positions remain covered by the canonical ARM comparison.
    expected = {
        "MapHeader": (48, {"mapLayout": 0, "events": 8, "mapScripts": 16, "connections": 24, "music": 32, "mapLayoutId": 34, "battleType": 43}),
        "MapLayout": (48, {"width": 0, "height": 4, "border": 8, "map": 16, "primaryTileset": 24, "secondaryTileset": 32, "isFrlg": 40}),
        "MapEvents": (40, {"objectEventCount": 0, "warpCount": 1, "coordEventCount": 2, "bgEventCount": 3, "objectEvents": 8, "warps": 16, "coordEvents": 24, "bgEvents": 32}),
        "ObjectEventTemplate": (28, {"localId": 0, "graphicsId": 1, "kind": 3, "x": 4, "y": 6, "script": 16, "flagId": 24, "filler": 26}),
        "WarpEvent": (8, {"x": 0, "y": 2, "elevation": 4, "warpId": 5, "mapNum": 6, "mapGroup": 7}),
        "CoordEvent": (24, {"x": 0, "y": 2, "elevation": 4, "trigger": 6, "index": 8, "script": 16}),
        "BgEvent": (16, {"x": 0, "y": 2, "elevation": 4, "kind": 5, "bgUnion": 8}),
        "MapConnections": (16, {"count": 0, "connections": 8}),
        "MapConnection": (12, {"direction": 0, "offset": 4, "mapGroup": 8, "mapNum": 9}),
        "SongHeader": (24, {"trackCount": 0, "blockCount": 1, "priority": 2, "reverb": 3, "tone": 8, "part": 16}),
        "Song": (16, {"header": 0, "ms": 8, "me": 10}),
        "RomToneData": (12, {"type": 0, "key": 1, "length": 2, "pan_sweep": 3, "wav": 4, "attack": 8, "decay": 9, "sustain": 10, "release": 11}),
    }
    probe, obj = os.path.join(tmp, "native_probe.c"), os.path.join(tmp, "native_probe.o")
    with open(probe, "w") as f:
        f.write('#include "global.h"\n#include "fieldmap.h"\n#include "m4a.h"\n')
        for name in expected:
            f.write(f"struct {name} native_{name};\n")
    run([host_cc, *host_cflags, *LAYOUT_DEFINES, "-DHOST_BUILD=1", "-std=gnu17", "-g", "-w", "-c",
         "-iquote", os.path.join(build_dir, "host_include"), probe, "-o", obj], cwd=refdir)
    actual = struct_layouts(obj, HOST_OBJDUMP, expected)
    for name, (size, fields) in expected.items():
        layout = actual.get(name)
        if not layout or layout[0] != size:
            print(f"FAIL native {name}: expected size {size}, got {layout}")
            return 1
        offsets = {field: bit // 8 for field, bit, _ in layout[1]}
        if any(offsets.get(field) != offset for field, offset in fields.items()):
            print(f"FAIL native {name}: expected offsets {fields}, got {offsets}")
            return 1
    print(f"native layout: {len(expected)} C overlays match the linked metadata schemas")
    return 0


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    refdir, build_dir, host_cc, host_cflags = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), sys.argv[3], sys.argv[4:]
    with tempfile.TemporaryDirectory() as tmp:
        macros = run([host_cc, *host_cflags, "-dM", "-E", "-x", "c", "-"], input="", text=True).stdout
        native64 = "#define __SIZEOF_POINTER__ 8" in macros
        failures = check_data(refdir, os.path.join(build_dir, "reference_asm"), tmp, host_cc if native64 else None)
        # Canonical ROM structures still have the GBA layout. Native structures
        # are instead validated through the actual executable's bytes below.
        failures += check_layout(refdir, build_dir, host_cc, [*host_cflags, "-m32"] if native64 else host_cflags, tmp)
        if native64:
            failures += check_native_layout(refdir, build_dir, host_cc, host_cflags, tmp)
            result = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "verify_native_data.py"),
                                     refdir, os.path.join(build_dir, "pkmemerald")])
            failures += result.returncode != 0
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
