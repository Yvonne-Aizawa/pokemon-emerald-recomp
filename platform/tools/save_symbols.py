"""Generate symbol tables; the host compiler evaluates upstream expressions."""
import pathlib
import re
import sys

root, output = map(pathlib.Path, sys.argv[1:])
with output.open('w') as out:
    for kind, prefix in [('flag', 'FLAG_'), ('var', 'VAR_')]:
        names = set()
        for path in (root / 'include/constants').glob(f'{kind}s*.h'):
            names.update(re.findall(r'^#define\s+(' + prefix + r'\w+)\s', path.read_text(), re.M))
        out.write(f'static const struct SaveSymbol s{kind.title()}Symbols[] = {{\n')
        for name in sorted(names):
            out.write(f'#ifdef {name}\n    {{{name}, "{name}"}},\n#endif\n')
        out.write('};\n')
