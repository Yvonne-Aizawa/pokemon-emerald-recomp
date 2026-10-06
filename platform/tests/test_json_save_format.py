"""The JSON save format (host_save_json.c), on the played checkpoints.

Conversion must not change what the game's loader sees, must be stable, must
apply edits to the readable fields, must refuse bad edits, must keep a
damaged chip raw, and must convert an old pkmemerald.sav on first start.
"""
import base64
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from inspect_save import inspect

SECTOR_SIZE = 4096
SIGNATURE = 0x08012025

manifest_path, output_dir, binary = map(pathlib.Path, sys.argv[1:])
manifest = json.loads(manifest_path.read_text())
work = pathlib.Path(tempfile.mkdtemp(prefix='pkmemerald-json-save-'))


def newest_slot(path):
    """The save blocks' sector data of a raw save's newest slot, by sector id."""
    data = path.read_bytes()
    sectors = {}
    for i in range(28):
        sector = data[i * SECTOR_SIZE:(i + 1) * SECTOR_SIZE]
        if int.from_bytes(sector[0xFF8:0xFFC], 'little') == SIGNATURE:
            sectors.setdefault(int.from_bytes(sector[0xFFC:0x1000], 'little'), {})[
                int.from_bytes(sector[0xFF4:0xFF6], 'little')] = sector[:0xFF4]
    return sectors[max(sectors)]


def convert(source, target):
    subprocess.run([str(binary.resolve()), '--convert-save', str(source), str(target)],
                   check=True, capture_output=True, text=True)


def rejected(document, message):
    path = work / 'rejected.json'
    path.write_text(json.dumps(document))
    result = subprocess.run([str(binary.resolve()), '--inspect-save', str(path)], capture_output=True, text=True)
    assert result.returncode != 0, f'accepted a save with {message}'
    return result.stderr


try:
    # Every checkpoint: same loaded state, stable through JSON -> raw -> JSON.
    for entry in manifest['checkpoints']:
        sav = output_dir / entry['id'] / 'pkmemerald.sav'
        first, raw, second = work / 'first.json', work / 'raw.sav', work / 'second.json'
        convert(sav, first)
        convert(first, raw)
        convert(raw, second)
        assert first.read_bytes() == second.read_bytes(), f"{entry['id']}: JSON not stable"
        expected = inspect(binary, sav)
        assert inspect(binary, first) == expected, f"{entry['id']}: JSON loads differently"
        assert inspect(binary, raw) == expected, f"{entry['id']}: rebuilt flash loads differently"
        # Every byte of the save blocks survives (the game zeroes padding,
        # which the JSON doesn't keep, so that matches too).
        original, rebuilt = newest_slot(sav), newest_slot(raw)
        for sector_id in range(14):
            assert original[sector_id] == rebuilt[sector_id], f"{entry['id']}: sector id {sector_id} differs"
        print(entry['id'], 'loads the same from JSON, converts stably, and keeps every save block byte')

    # Edits to the readable fields reach the game's loader.
    last = output_dir / manifest['checkpoints'][-1]['id'] / 'pkmemerald.sav'
    base = work / 'base.json'
    convert(last, base)
    document = json.loads(base.read_text())
    before = inspect(binary, base)
    game = document['game']
    game['player']['name'] = 'Édith'
    game['player']['money'] = 123456
    removed = game['flags'].pop(0)
    game['flags'].append('FLAG_BADGE01_GET')
    game['vars']['VAR_LITTLEROOT_TOWN_STATE'] = 3
    edited = work / 'edited.json'
    edited.write_text(json.dumps(document, ensure_ascii=False))
    after = inspect(binary, edited)
    # Édith in the game's charset (charmap.txt), then the terminator.
    assert after['player_name_bytes'][:6] == [0x06, 0xD8, 0xDD, 0xE8, 0xDC, 0xFF], after['player_name_bytes']
    assert after['money'] == 123456
    badge = next(k for k, names in after['flags'].items() if 'FLAG_BADGE01_GET' in names)
    assert badge not in before['flags']
    assert all(removed not in names for names in after['flags'].values())
    assert next(v['value'] for v in after['vars'].values() if 'VAR_LITTLEROOT_TOWN_STATE' in v['names']) == 3
    for key in ('party', 'bag', 'map', 'gender', 'save_counter'):
        assert after[key] == before[key], key
    print('edits to name, money, flags and vars load')

    # Save block fields are named: edits reach the loader, and the file
    # doesn't depend on the block sizes.
    document = json.loads(base.read_text())
    blocks = document['game']['blocks']
    blocks['save_block_1']['pos']['x'] = 3
    for name in document['layout']:
        document['layout'][name] += 4
    edited.write_text(json.dumps(document))
    after = inspect(binary, edited)
    assert after['map']['x'] == 3 and after['map']['y'] == before['map']['y']
    print('edits to save block fields load, whatever the recorded layout')

    # Version 1 (the blocks as base64 images) still loads. Build one from the
    # .sav's newest slot, decoded here independently of the game.
    document = json.loads(base.read_text())
    slot = newest_slot(last)
    sizes = document['layout']
    images = {
        'save_block_2': slot[0][:3968],
        'save_block_1': b''.join(slot[i][:3968] for i in range(1, 5)),
        'pokemon_storage': b''.join(slot[i][:3968] for i in range(5, 14)),
        'save_block_3': b''.join(slot[i][3968:4084] for i in range(14)),
    }
    document['version'] = 1
    document['game']['blocks'] = {name: base64.b64encode(image[:sizes[name]]).decode() for name, image in images.items()}
    version1 = work / 'version1.json'
    version1.write_text(json.dumps(document))
    assert inspect(binary, version1) == inspect(binary, last)
    print('a version 1 file loads the same')

    # Bad edits are refused with a message, leaving the file alone.
    def edit(change):
        copy = json.loads(base.read_text())
        change(copy)
        return copy
    for change, message in [
        (lambda d: d['game']['flags'].append('FLAG_NO_SUCH_FLAG'), 'unknown flag'),
        (lambda d: d['game']['vars'].update({'VAR_NOPE': 1}), 'unknown var'),
        (lambda d: d['game']['player'].update(money=10 ** 7), 'whole number'),
        (lambda d: d['game']['player'].update(name='Toolongname'), 'longer than'),
        (lambda d: d['game']['player'].update(name='€'), 'no character'),
        (lambda d: d['game']['blocks']['save_block_2'].update(nope=1), 'no such field in SaveBlock2'),
        (lambda d: d['game']['blocks']['save_block_2'].update(optionsTextSpeed=8), 'from 0 to 7'),
        (lambda d: d['game']['blocks']['save_block_1'].update(money=5), 'set this in game.player.money'),
        (lambda d: d['game']['blocks']['save_block_1']['pos'].update(x=-40000), 'from -32768 to 32767'),
        (lambda d: d['game']['blocks']['save_block_1'].update(mapView=[0] * 257), 'at most 256 entries'),
        (lambda d: d['game']['blocks'].update(save_block_9={}), 'no such save block'),
        (lambda d: d.update(version=99), 'unsupported version'),
    ]:
        stderr = rejected(edit(change), message)
        assert message in stderr, f'{message!r} not in {stderr!r}'
    print('bad edits are refused')

    # A chip the loader doesn't take cleanly (here: a damaged sector in the
    # newest slot) is kept raw, byte for byte.
    data = bytearray(last.read_bytes())
    footers = [(int.from_bytes(data[i * SECTOR_SIZE + 0xFFC:i * SECTOR_SIZE + 0x1000], 'little'), i)
               for i in range(28)
               if int.from_bytes(data[i * SECTOR_SIZE + 0xFF8:i * SECTOR_SIZE + 0xFFC], 'little') == SIGNATURE]
    newest = max(footers)[1]
    data[newest * SECTOR_SIZE] ^= 0xFF
    damaged, damaged_json, damaged_back = work / 'damaged.sav', work / 'damaged.json', work / 'damaged-back.sav'
    damaged.write_bytes(data)
    convert(damaged, damaged_json)
    raw_document = json.loads(damaged_json.read_text())
    assert 'flash' in raw_document and 'game' not in raw_document
    convert(damaged_json, damaged_back)
    assert damaged_back.read_bytes() == bytes(data)
    print('a damaged chip is kept raw')

    # First start with an old pkmemerald.sav: converted, and the old file kept.
    saves = work / 'saves'
    saves.mkdir()
    shutil.copy(last, saves / 'pkmemerald.sav')
    result = subprocess.run([str(binary.resolve()), '--fast', '-f', '30', '-s', str(saves)],
                            capture_output=True, text=True, check=True)
    assert 'converting pkmemerald.sav' in result.stdout, result.stdout
    assert (saves / 'pkmemerald.sav').read_bytes() == last.read_bytes()
    assert inspect(binary, saves / 'pkmemerald.json') == inspect(binary, last)
    print('an old pkmemerald.sav is converted and kept')
finally:
    shutil.rmtree(work)
