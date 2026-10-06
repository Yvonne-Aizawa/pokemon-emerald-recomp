#!/usr/bin/env python3
"""Losslessly archive a 128 KiB flash save as JSON, or restore its exact bytes."""
import argparse
import base64
import hashlib
import json
import pathlib
import subprocess
import tempfile

from inspect_save import inspect

FORMAT = 'pkmemerald-flash-archive'
VERSION = 1
SECTOR_SIZE = 4096
SECTOR_COUNT = 32
FLASH_SIZE = SECTOR_SIZE * SECTOR_COUNT
MAX_JSON_SIZE = 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_save(path):
    with path.open('rb') as source:
        data = source.read(FLASH_SIZE + 1)
    if len(data) != FLASH_SIZE:
        raise ValueError('save must be exactly 131072 bytes')
    return data


def inspection_digest(state):
    return digest(json.dumps(state, sort_keys=True, separators=(',', ':')).encode('utf-8'))


def archive(data, filename, state=None):
    if len(data) != FLASH_SIZE:
        raise ValueError('save must be exactly 131072 bytes')
    document = {
        'format': FORMAT,
        'schema_version': VERSION,
        'source': {'file': filename, 'size_bytes': len(data), 'sha256': digest(data)},
        'flash': {'sector_size': SECTOR_SIZE, 'sector_count': SECTOR_COUNT, 'sectors': []},
    }
    for index in range(SECTOR_COUNT):
        sector = data[index * SECTOR_SIZE:(index + 1) * SECTOR_SIZE]
        document['flash']['sectors'].append({
            'index': index,
            'encoding': 'base64',
            'data': base64.b64encode(sector).decode('ascii'),
            'sha256': digest(sector),
        })
    if state is not None:
        document['inspection'] = {
            'purpose': 'read-only projection; restoration uses flash sectors',
            'state': state,
            'sha256': inspection_digest(state),
        }
    return document


def restore(document):
    """Validate an unmodified archive before producing any output bytes."""
    if not isinstance(document, dict) or document.get('format') != FORMAT:
        raise ValueError('not a full flash archive (inspection reports cannot be restored)')
    if type(document.get('schema_version')) is not int or document['schema_version'] != VERSION:
        raise ValueError('unsupported flash archive version')
    flash = document.get('flash')
    if not isinstance(flash, dict) or type(flash.get('sector_size')) is not int or type(flash.get('sector_count')) is not int:
        raise ValueError('invalid flash geometry')
    if flash['sector_size'] != SECTOR_SIZE or flash['sector_count'] != SECTOR_COUNT:
        raise ValueError('unsupported flash geometry')
    sectors = flash.get('sectors')
    if not isinstance(sectors, list) or len(sectors) != SECTOR_COUNT:
        raise ValueError('archive must contain all 32 sectors')
    chunks = []
    for index, sector in enumerate(sectors):
        if not isinstance(sector, dict) or type(sector.get('index')) is not int or sector['index'] != index:
            raise ValueError('sectors must have unique indices in flash order')
        if sector.get('encoding') != 'base64' or not isinstance(sector.get('data'), str):
            raise ValueError(f'sector {index}: unsupported encoding')
        # Bound decoding and require canonical ASCII Base64, including padding.
        encoded = sector['data']
        if len(encoded) != ((SECTOR_SIZE + 2) // 3) * 4:
            raise ValueError(f'sector {index}: invalid encoded length')
        try:
            chunk = base64.b64decode(encoded, validate=True)
        except ValueError as error:
            raise ValueError(f'sector {index}: invalid Base64') from error
        if len(chunk) != SECTOR_SIZE or base64.b64encode(chunk).decode('ascii') != encoded:
            raise ValueError(f'sector {index}: invalid sector data')
        if digest(chunk) != sector.get('sha256'):
            raise ValueError(f'sector {index}: SHA-256 mismatch')
        chunks.append(chunk)
    data = b''.join(chunks)
    source = document.get('source')
    if not isinstance(source, dict) or type(source.get('size_bytes')) is not int or source['size_bytes'] != FLASH_SIZE:
        raise ValueError('invalid source size')
    if digest(data) != source.get('sha256'):
        raise ValueError('whole-save SHA-256 mismatch')
    if 'inspection' in document:
        view = document['inspection']
        if not isinstance(view, dict) or not isinstance(view.get('state'), dict):
            raise ValueError('invalid inspection projection')
        if inspection_digest(view['state']) != view.get('sha256'):
            raise ValueError('inspection was edited; decoded field editing is not supported')
    return data


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate JSON key: {key}')
        result[key] = value
    return result


def read_archive(path):
    with path.open('rb') as source:
        data = source.read(MAX_JSON_SIZE + 1)
    if len(data) > MAX_JSON_SIZE:
        raise ValueError('archive exceeds the 1 MiB input limit')
    return json.loads(data, object_pairs_hook=unique_keys)


def write_new(path, data):
    """Never overwrite an existing save/report, including symlinks."""
    output = path.open('xb')
    try:
        with output:
            output.write(data)
    except BaseException:
        path.unlink()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    export = commands.add_parser('export', help='preserve all flash bytes in JSON')
    export.add_argument('save', type=pathlib.Path)
    export.add_argument('-o', '--output', type=pathlib.Path, required=True)
    export.add_argument('--binary', type=pathlib.Path, help='optionally include decoded state using this matching game build')
    load = commands.add_parser('import', help='restore exact original flash bytes to a new file')
    load.add_argument('json', type=pathlib.Path)
    load.add_argument('-o', '--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'export':
            data = read_save(args.save)
            state = None
            if args.binary:
                # Inspect the same snapshot being archived, even if the original
                # file changes while the tool is running. No game writes occur.
                with tempfile.TemporaryDirectory(prefix='pkm-archive-') as folder:
                    snapshot = pathlib.Path(folder) / 'snapshot.sav'
                    snapshot.write_bytes(data)
                    state = inspect(args.binary, snapshot)
            document = archive(data, args.save.name, state)
            output = (json.dumps(document, indent=2, sort_keys=True) + '\n').encode('utf-8')
        else:
            output = restore(read_archive(args.json))
        write_new(args.output, output)
        print(f'Wrote {args.output}')
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            parser.exit(1, error.stderr)
        parser.exit(1, f'archive: {error}\n')


if __name__ == '__main__':
    main()
