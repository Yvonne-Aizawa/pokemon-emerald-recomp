#!/usr/bin/env python3
"""Inspect played checkpoints, or compare them, without modifying either file."""
import argparse
import hashlib
import json
import pathlib
import subprocess


def inspect(binary, path):
    before = hashlib.sha256(path.read_bytes()).digest()
    result = subprocess.run([str(binary.resolve()), '--inspect-save', str(path)],
                            check=True, text=True, capture_output=True)
    if hashlib.sha256(path.read_bytes()).digest() != before:
        raise RuntimeError(f'inspector changed {path}')
    return json.loads(result.stdout)


def compare(before, after):
    changes = {}
    for key in before.keys() | after.keys():
        if key in ('flags', 'vars'):
            a, b = before.get(key, {}), after.get(key, {})
            # Absent flags are clear; absent variables have value zero.
            changed = {k: {'before': a.get(k), 'after': b.get(k)}
                       for k in sorted(a.keys() | b.keys(), key=int)
                       if a.get(k) != b.get(k)}
            if changed:
                changes[key] = changed
        elif before.get(key) != after.get(key):
            changes[key] = {'before': before.get(key), 'after': after.get(key)}
    return changes


def export_report(path, source, report):
    """Export a versioned inspection projection, never a replacement save."""
    document = {
        "format": "pkmemerald-save-inspection",
        "schema_version": 1,
        "source": {
            "file": source.name,
            "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "size_bytes": source.stat().st_size,
        },
        "state": report,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation prevents overwriting saves or previously saved reports.
    with path.open('x', encoding='utf-8') as output:
        output.write(json.dumps(document, indent=2, sort_keys=True) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('save', type=pathlib.Path, nargs='+')
    parser.add_argument('--compare', type=pathlib.Path, help='later checkpoint (one input only)')
    parser.add_argument('--binary', type=pathlib.Path, default=pathlib.Path('build/pkmemerald'))
    output = parser.add_mutually_exclusive_group()
    output.add_argument('-o', '--output', type=pathlib.Path, help='write one versioned JSON report')
    output.add_argument('--output-dir', type=pathlib.Path, help='export each save as NAME.json')
    args = parser.parse_args()
    if args.compare and (len(args.save) != 1 or args.output or args.output_dir):
        parser.error('--compare requires one save and prints its diff to stdout')
    if args.output and len(args.save) != 1:
        parser.error('--output requires one save; use --output-dir for several')
    if len(args.save) > 1 and args.output_dir is None:
        parser.error('several saves require --output-dir')

    targets = []
    if args.output:
        targets = [args.output]
    elif args.output_dir:
        targets = [args.output_dir / (p.stem + '.json') for p in args.save]
    resolved = [p.resolve() for p in targets]
    if len(set(resolved)) != len(resolved):
        parser.error('input filenames would produce duplicate JSON paths')
    sources = {p.resolve() for p in args.save}
    if any(p in sources or p.exists() for p in resolved):
        parser.error('output would overwrite an input or an existing file; choose a new path')

    try:
        # Validate every input before creating any reports.
        reports = [inspect(args.binary, p) for p in args.save]
        if targets:
            for source, target, report in zip(args.save, targets, reports):
                export_report(target, source, report)
                print(f'{source} -> {target}')
        else:
            report = reports[0]
            if args.compare:
                report = compare(report, inspect(args.binary, args.compare))
            print(json.dumps(report, indent=2, sort_keys=True))
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            parser.exit(1, error.stderr)
        parser.exit(1, f'inspect: {error}\n')


if __name__ == '__main__':
    main()
