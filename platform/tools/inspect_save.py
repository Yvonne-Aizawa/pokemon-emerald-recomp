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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('save', type=pathlib.Path)
    parser.add_argument('--compare', type=pathlib.Path, help='later checkpoint')
    parser.add_argument('--binary', type=pathlib.Path, default=pathlib.Path('build/pkmemerald'))
    args = parser.parse_args()
    report = inspect(args.binary, args.save)
    if args.compare:
        report = compare(report, inspect(args.binary, args.compare))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
