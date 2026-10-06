#!/usr/bin/env python3
"""Restore reviewed checkpoint archives to isolated test directories."""
import argparse
import json
import pathlib

from save_archive import digest, read_archive, restore


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=pathlib.Path)
    parser.add_argument('output_dir', type=pathlib.Path)
    args = parser.parse_args()
    try:
        manifest = json.loads(args.manifest.read_text())
        if manifest['schema_version'] != 1:
            raise ValueError('unsupported checkpoint manifest version')
        pending = []
        ids = set()
        for entry in manifest['checkpoints']:
            name = entry['id']
            filename = entry['archive']
            if not name or pathlib.Path(name).name != name or name in ('.', '..') or name in ids:
                raise ValueError('invalid or duplicate checkpoint id')
            if pathlib.Path(filename).name != filename or filename in ('.', '..'):
                raise ValueError('invalid archive filename')
            ids.add(name)
            document = read_archive(args.manifest.parent / filename)
            data = restore(document)
            if digest(data) != entry['sha256']:
                raise ValueError(f'{name}: manifest hash mismatch')
            state = document['inspection']['state']
            if any(state.get(key) != value for key, value in entry['expected'].items()):
                raise ValueError(f'{name}: expected checkpoint state mismatch')
            pending.append((name, data))
        # Validate the entire set before changing any generated fixture.
        for name, data in pending:
            folder = args.output_dir / name
            folder.mkdir(parents=True, exist_ok=True)
            output = folder / 'pkmemerald.sav'
            # Recreate disposable build outputs on every fixture setup; game
            # runs must never reuse a save modified by an earlier run.
            if output.is_symlink():
                raise ValueError(f'{name}: refusing a symlink output')
            output.write_bytes(data)
            if digest(output.read_bytes()) != digest(data):
                raise ValueError(f'{name}: restored hash mismatch')
            print(f'{name}: restored {len(data)} bytes; SHA-256 verified')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'checkpoints: {error}\n')


if __name__ == '__main__':
    main()
