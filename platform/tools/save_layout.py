#!/usr/bin/env python3
"""Generate the save-block field table (platform/src/host_save_layout.inc).

Reads the layout of the game's save blocks from the debug info of the
save-layout-probe object (platform/tools/save_layout_probe.c) and writes the
table host_save_json.c walks to write and read every field by name, plus
compile-time checks of each type's size and member offsets: if upstream
changes a save struct, the build fails until the table is regenerated.

    save_layout.py PROBE.o OUTPUT.inc            write the table
    save_layout.py PROBE.o OUTPUT.inc --check    fail if OUTPUT.inc is stale

Usually run as `cmake --build build --target save-layout`. Needs pyelftools
and an ELF object (a Linux build).
"""
import argparse
import hashlib
import pathlib
import sys

from elftools.elf.elffile import ELFFile

# The roots, in the order host_save_layout.h numbers them.
ROOTS = ['SaveBlock2', 'SaveBlock1', 'PokemonStorage', 'SaveBlock3']
MAX_DIMS = 3

DW_ATE_SIGNED = {0x05, 0x06}        # signed, signed_char
DW_ATE_UNSIGNED = {0x02, 0x07, 0x08}  # boolean, unsigned, unsigned_char


def name_of(die):
    attr = die.attributes.get('DW_AT_name')
    return attr.value.decode() if attr else None


def type_of(die):
    return die.get_DIE_from_attribute('DW_AT_type') if 'DW_AT_type' in die.attributes else None


def strip(die):
    """Skip typedefs and qualifiers; returns (die, typedef name or None)."""
    typedef = None
    while die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_atomic_type'):
        if die.tag == 'DW_TAG_typedef' and typedef is None:
            typedef = name_of(die)
        die = type_of(die)
    return die, typedef


def scalar_kind(die):
    """(kind, size) for a base type, enum or pointer."""
    size = die.attributes['DW_AT_byte_size'].value if 'DW_AT_byte_size' in die.attributes else 4
    if die.tag == 'DW_TAG_pointer_type':
        return 'UINT', size
    if die.tag == 'DW_TAG_enumeration_type':
        underlying = type_of(die)
        if underlying is not None:
            return scalar_kind(strip(underlying)[0])[0], size
        negative = any(c.attributes['DW_AT_const_value'].value < 0 for c in die.iter_children()
                       if c.tag == 'DW_TAG_enumerator')
        return ('SINT' if negative else 'UINT'), size
    if die.tag == 'DW_TAG_base_type':
        encoding = die.attributes['DW_AT_encoding'].value
        if encoding in DW_ATE_SIGNED:
            return 'SINT', size
        if encoding in DW_ATE_UNSIGNED:
            return 'UINT', size
    raise SystemExit(f'save_layout: unsupported type {die.tag} {name_of(die)}')


def array_dims(die):
    """Dimensions of an array type (typedef'd inner arrays flattened) and its element type."""
    dims = []
    while die.tag == 'DW_TAG_array_type':
        for sub in die.iter_children():
            if sub.tag != 'DW_TAG_subrange_type':
                continue
            if 'DW_AT_count' in sub.attributes:
                dims.append(sub.attributes['DW_AT_count'].value)
            elif 'DW_AT_upper_bound' in sub.attributes:
                dims.append(sub.attributes['DW_AT_upper_bound'].value + 1)
            else:
                dims.append(0)
        die = strip(type_of(die))[0]
    return dims, die


class Generator:
    def __init__(self):
        self.types = []      # dicts: name, size, fields
        self.by_offset = {}  # DIE offset -> type index
        self.asserts = []

    def struct_type(self, die, cname, anchor):
        """Index of the table entry for struct `die`. Its layout is checked
        against `cname`, or, for a type C can't name, through `anchor`: the
        named ancestor, the member path to this type, and its offset there."""
        if die.offset in self.by_offset and self.types[self.by_offset[die.offset]]['done']:
            return self.by_offset[die.offset]
        index = self.reserve(die, cname)
        entry = self.types[index]
        entry['done'] = True
        size = entry['size']
        if cname:
            anchor = (cname, '', 0)
            self.asserts.append(f'_Static_assert(sizeof({cname}) == {size}, "{cname}: regenerate the save layout");')
        self.add_members(entry['fields'], die, 0, anchor)
        return index

    def reserve(self, die, cname):
        """The table index for struct `die`, allocating it if new."""
        if die.offset not in self.by_offset:
            index = len(self.types)
            self.types.append({'name': name_of(die) or cname or f'anonymous_{index}',
                               'size': die.attributes['DW_AT_byte_size'].value, 'fields': [], 'done': False})
            self.by_offset[die.offset] = index
        return self.by_offset[die.offset]

    def add_members(self, fields, die, base, anchor):
        for member in die.iter_children():
            if member.tag != 'DW_TAG_member':
                continue
            name = name_of(member)
            mtype, typedef = strip(type_of(member))
            if 'DW_AT_data_bit_offset' in member.attributes:
                bit = base * 8 + member.attributes['DW_AT_data_bit_offset'].value
                bits = member.attributes['DW_AT_bit_size'].value
                kind, _ = scalar_kind(mtype)
                fields.append({'name': name, 'offset': bit // 8, 'size': (bit % 8 + bits + 7) // 8,
                               'kind': kind, 'bit_offset': bit % 8, 'bit_size': bits, 'dims': [], 'type': 0})
                continue
            if 'DW_AT_bit_size' in member.attributes:
                raise SystemExit('save_layout: old-style DWARF bitfields (DW_AT_bit_offset) are not supported')
            offset = base + member.attributes['DW_AT_data_member_location'].value
            if name is None and mtype.tag == 'DW_TAG_structure_type':
                # An anonymous struct member: its fields belong to the parent.
                self.add_members(fields, mtype, offset, anchor)
                continue
            path = None
            if name is not None and anchor is not None:
                anchor_name, prefix, origin = anchor
                path = prefix + name
                self.asserts.append(f'_Static_assert(offsetof({anchor_name}, {path}) == {origin + offset}, '
                                    f'"{anchor_name}.{path}: regenerate the save layout");')
            if name is None:
                name = f'anonymous_{offset}'
            dims, element = array_dims(mtype)
            if len(dims) > MAX_DIMS:
                raise SystemExit(f'save_layout: {name} has more than {MAX_DIMS} dimensions')
            element_typedef = typedef if not dims else None
            if dims:
                element, element_typedef = strip(element)
            field = {'name': name, 'offset': offset, 'bit_offset': 0, 'bit_size': 0, 'dims': dims, 'type': 0}
            if element.tag == 'DW_TAG_structure_type':
                tag_name = name_of(element)
                cname = f'struct {tag_name}' if tag_name else element_typedef
                inner = None
                if path is not None:
                    inner = (anchor[0], path + '[0]' * len(dims) + '.', anchor[2] + offset)
                field['kind'] = 'STRUCT'
                field['size'] = element.attributes['DW_AT_byte_size'].value
                field['type'] = self.struct_type(element, cname, inner)
            elif element.tag == 'DW_TAG_union_type':
                field['kind'] = 'BYTES'
                field['size'] = element.attributes['DW_AT_byte_size'].value
            else:
                field['kind'], field['size'] = scalar_kind(element)
            fields.append(field)


def find_roots(path):
    with open(path, 'rb') as f:
        dwarf = ELFFile(f).get_dwarf_info()
        found = {}
        for cu in dwarf.iter_CUs():
            for die in cu.get_top_DIE().iter_children():
                if (die.tag == 'DW_TAG_structure_type' and name_of(die) in ROOTS
                        and 'DW_AT_declaration' not in die.attributes):
                    found.setdefault(name_of(die), die)
            if len(found) == len(ROOTS):
                return generate([found[name] for name in ROOTS])
    raise SystemExit(f'save_layout: {path} has no debug info for {sorted(set(ROOTS) - set(found))}')


def generate(roots):
    gen = Generator()
    for die in roots:
        gen.reserve(die, None)  # the roots are types 0-3
    for die in roots:
        gen.struct_type(die, f'struct {name_of(die)}', None)
    lines = [
        '/* Generated by platform/tools/save_layout.py from the debug info of',
        ' * platform/tools/save_layout_probe.c: do not edit. Regenerate with',
        ' * `cmake --build build --target save-layout` after upstream changes a save',
        ' * struct (the checks at the end then fail to compile). */',
        '',
        'const struct HostSaveType gHostSaveTypes[] = {',
    ]
    first = 0
    for index, entry in enumerate(gen.types):
        lines.append(f'    /* {index} */ {{ "{entry["name"]}", {entry["size"]}, {first}, {len(entry["fields"])} }},')
        first += len(entry['fields'])
    lines += ['};', '', 'const struct HostSaveField gHostSaveFields[] = {']
    for entry in gen.types:
        lines.append(f'    /* {entry["name"]} */')
        for field in entry['fields']:
            dims = ', '.join(str(d) for d in field['dims']) or '0'
            lines.append(f'    {{ "{field["name"]}", {field["offset"]}, {field["size"]}, HOST_SAVE_{field["kind"]}, '
                         f'{field["bit_offset"]}, {field["bit_size"]}, {len(field["dims"])}, {{ {dims} }}, {field["type"]} }},')
    lines += ['};', '', f'const unsigned gHostSaveTypeCount = {len(gen.types)};']
    # Saves record this: a different one means another layout (upstream
    # version), whose fields may not all exist here.
    fingerprint = hashlib.sha256('\n'.join(lines[5:]).encode()).hexdigest()[:16]
    lines += [f'const char gHostSaveLayoutFingerprint[] = "{fingerprint}";', '']
    lines += gen.asserts
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('probe', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    text = find_roots(args.probe)
    if args.check:
        if not args.output.exists() or args.output.read_text() != text:
            print(f'{args.output} is stale: run `cmake --build build --target save-layout`', file=sys.stderr)
            return 1
        print(f'{args.output} matches the build')
        return 0
    args.output.write_text(text)
    print(f'wrote {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
