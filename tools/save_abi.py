#!/usr/bin/env python3
"""Generate raw/native SaveBlock1 copy ranges from the checked save schemas."""
import math
import pathlib
import re
import sys


def schema(path):
    text = pathlib.Path(path).read_text()
    types = [(name, int(size), int(start), int(count)) for name, size, start, count in
             re.findall(r'/\* \d+ \*/ \{ "([^"]+)", (\d+), (\d+), (\d+) \}', text)]
    fields = []
    section = text.split('const struct HostSaveField gHostSaveFields[] = {')[1].split('};')[0]
    for name, off, size, kind, bit, bits, ndim, dims, child, names in re.findall(
            r'\{ "([^"]+)", (\d+), (\d+), (HOST_SAVE_\w+), (\d+), (\d+), (\d+), \{ ([^}]+) \}, (\d+), (\d+) \}', section):
        fields.append((name, int(off), int(size), kind, math.prod(map(int, dims.split(', '))) if int(ndim) else 1, int(child)))
    return types, fields


def ranges(raw, native, type_id=1, raw_base=0, native_base=0):
    rt, rf = raw
    nt, nf = native
    a = rf[rt[type_id][2]:rt[type_id][2] + rt[type_id][3]]
    b = nf[nt[type_id][2]:nt[type_id][2] + nt[type_id][3]]
    assert len(a) == len(b)
    for r, n in zip(a, b):
        assert r[0] == n[0] and r[3:] == n[3:], (r, n)
        for i in range(r[4]):
            ro, no = raw_base + r[1] + i * r[2], native_base + n[1] + i * n[2]
            if r[3] == 'HOST_SAVE_STRUCT':
                yield from ranges(raw, native, r[5], ro, no)
            else:
                yield (ro, no, r[2], n[2])


def main():
    raw, native = schema(sys.argv[1]), schema(sys.argv[2])
    entries = []
    for entry in ranges(raw, native):
        if entries and entry == entries[-1]:  # bitfields share containers
            continue
        if entries and entries[-1][2] == entries[-1][3] and entry[2] == entry[3]:
            ro, no, rs, ns = entries[-1]
            if ro + rs == entry[0] and no + ns == entry[1]:
                entries[-1] = (ro, no, rs + entry[2], ns + entry[3])
                continue
        entries.append(entry)
    output = ['/* Generated from the checked raw and native save schemas. */',
              f'#define RAW_BLOCK_SIZE {raw[0][1][1]}',
              f'_Static_assert(sizeof(struct SaveBlock1) == {native[0][1][1]}, "regenerate save ABI map");',
              'static const struct { size_t raw, native, rawSize, nativeSize; } sSaveAbiRanges[] = {']
    output += ['    { %d, %d, %d, %d },' % entry for entry in entries]
    output += ['};', '']
    pathlib.Path(sys.argv[3]).write_text('\n'.join(output))


if __name__ == '__main__':
    main()
