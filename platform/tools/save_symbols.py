"""Generate symbol tables; the host compiler evaluates upstream expressions."""
import pathlib
import re
import sys

root, output = map(pathlib.Path, sys.argv[1:])


def charmap():
    """Single game-text bytes that stand for a single character, from upstream's
    charmap.txt. Only bytes that round-trip (the character's first byte is this
    one) are kept; the save file writes other bytes as {XX} escapes, so '{' and
    '}' are reserved, as is the 0xFF terminator."""
    first_byte = {}
    pairs = []
    for line in (root / 'charmap.txt').read_text(encoding='utf-8').splitlines():
        match = re.match(r"^'(\\'|[^'])'\s*=\s*([0-9A-Fa-f]{2})\s*(@.*)?$", line)
        if not match:
            continue
        char = "'" if match.group(1) == "\\'" else match.group(1)
        byte = int(match.group(2), 16)
        first_byte.setdefault(char, byte)
        pairs.append((byte, char))
    table = {}
    for byte, char in pairs:
        if byte != 0xFF and char not in '{}' and first_byte[char] == byte:
            table.setdefault(byte, char)
    return sorted(table.items())


def c_string(text):
    return '"' + ''.join(f'\\x{b:02x}' if b >= 0x80 or b < 0x20 or b in b'"\\?'
                         else chr(b) for b in text.encode('utf-8')) + '"'


with output.open('w') as out:
    for kind, prefix in [('flag', 'FLAG_'), ('var', 'VAR_')]:
        names = set()
        for path in (root / 'include/constants').glob(f'{kind}s*.h'):
            names.update(re.findall(r'^#define\s+(' + prefix + r'\w+)\s', path.read_text(), re.M))
        out.write(f'static const struct SaveSymbol s{kind.title()}Symbols[] = {{\n')
        for name in sorted(names):
            out.write(f'#ifdef {name}\n    {{{name}, "{name}"}},\n#endif\n')
        out.write('};\n')
    # Only the save file's writer wants this one (and unused statics warn).
    out.write('#ifdef SAVE_SYMBOLS_CHARMAP\nstatic const struct SaveSymbol sCharSymbols[] = {\n')
    for byte, char in charmap():
        out.write(f'    {{0x{byte:02X}, {c_string(char)}}},\n')
    out.write('};\n#endif\n')
