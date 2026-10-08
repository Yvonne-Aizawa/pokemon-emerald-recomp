#!/usr/bin/env python3
"""Check linked 64-bit map and audio metadata against upstream JSON/assembly.

This deliberately reads the executable's bytes independently of the assembler
adapter and the C overlays, so wrong pointer widths/strides cannot agree with
one another and silently pass.
"""
import json
import pathlib
import re
import struct
import subprocess
import sys


class Image:
    def __init__(self, binary):
        self.data = binary.read_bytes()
        self.symbols = {}
        for line in subprocess.check_output(['nm', '-a', str(binary)], text=True).splitlines():
            fields = line.split()
            if len(fields) == 3:
                try:
                    self.symbols[fields[2]] = int(fields[0], 16)
                except ValueError:
                    pass
        self.sections = []
        for line in subprocess.check_output(['objdump', '-h', str(binary)], text=True).splitlines():
            fields = line.split()
            if len(fields) >= 7 and fields[0].isdigit():
                self.sections.append((int(fields[3], 16), int(fields[2], 16), int(fields[5], 16)))

    def read(self, address, size):
        for base, length, offset in self.sections:
            if base <= address and address + size <= base + length:
                return self.data[offset + address - base:offset + address - base + size]
        raise AssertionError(f'unmapped pointer {address:#x}, size {size}')

    def unpack(self, fmt, address):
        return struct.unpack(fmt, self.read(address, struct.calcsize(fmt)))

    def symbol(self, name):
        if name in ('NULL', '0', '0x0'):
            return 0
        return self.symbols[name]


def verify_maps(ref, image):
    layouts = json.loads((ref / 'data/layouts/layouts.json').read_text())['layouts']
    by_id = {layout['id']: layout for layout in layouts}
    for i, layout in enumerate(layouts):
        if layout['name'] not in image.symbols:
            assert image.unpack('<Q', image.symbol('gMapLayouts') + i * 8)[0] == 0, layout['name']
            continue
        address = image.symbol(layout['name'])
        assert image.unpack('<Q', image.symbol('gMapLayouts') + i * 8)[0] == address, layout['name']
        width, height, border, blocks, primary, secondary, *_ = image.unpack('<iiQQQQBBBB4x', address)
        assert (width, height) == (layout['width'], layout['height']), layout['name']
        assert primary == image.symbol(layout['primary_tileset']) and secondary == image.symbol(layout['secondary_tileset']), layout['name']
        for pointer, path in ((border, layout['border_filepath']), (blocks, layout['blockdata_filepath'])):
            expected = (ref / path).read_bytes()
            assert image.read(pointer, len(expected)) == expected, path

    groups = json.loads((ref / 'data/maps/map_groups.json').read_text())
    map_ids = {name: (group, num) for group, key in enumerate(groups['group_order']) for num, name in enumerate(groups[key])}
    source_ids = {name: json.loads((ref / 'data/maps' / name / 'map.json').read_text())['id'] for name in map_ids if (ref / 'data/maps' / name / 'map.json').exists()}
    count = 0
    for group, key in enumerate(groups['group_order']):
        if key not in image.symbols:
            assert image.unpack('<Q', image.symbol('gMapGroups') + group * 8)[0] == 0, key
            continue
        assert image.unpack('<Q', image.symbol('gMapGroups') + group * 8)[0] == image.symbol(key), key
        for num, name in enumerate(groups[key]):
            if name not in image.symbols:
                assert image.unpack('<Q', image.symbol(key) + num * 8)[0] == 0, name
                continue
            path = ref / 'data/maps' / name / 'map.json'
            m = json.loads(path.read_text())
            address = image.symbol(name)
            assert image.unpack('<Q', image.symbol(key) + num * 8)[0] == address, name
            layout, events, scripts, connections, _, layout_id, _, _, _, _, _, flags, _ = image.unpack('<QQQQHHBBBbHBB4x', address)
            assert layout == image.symbol(by_id[m['layout']]['name']), name
            assert layouts[layout_id - 1]['id'] == m['layout'], name
            header_pointers = re.findall(r'\.4byte\s+(\w+)', (path.parent / 'header.inc').read_text())[:4]
            assert scripts == image.symbol(header_pointers[2]), name
            expected_flags = sum(int(m.get(field, False)) << bit for bit, field in enumerate(
                ['allow_cycling', 'allow_escaping', 'allow_running', 'show_map_name', 'write_specialvar_iseffect', 'requires_flash']))
            assert flags == expected_flags, name
            event_source = json.loads((ref / 'data/maps' / m['shared_events_map'] / 'map.json').read_text()) if 'shared_events_map' in m else m
            source_events = [event_source.get(field, []) for field in ('object_events', 'warp_events', 'coord_events', 'bg_events')]
            values = image.unpack('<4B4x4Q', events)
            assert list(values[:4]) == list(map(len, source_events)), name
            for i, event in enumerate(source_events[0]):
                local, gfx, kind, x, y, detail, script, flag, filler = image.unpack('<BHBhh8sQHH', values[4] + i * 28)
                assert (x, y) == (event['x'], event['y']), (name, 'object', i)
                if 'script' in event:
                    assert script == image.symbol(event['script']), (name, 'object script', i)
            for i, event in enumerate(source_events[1]):
                x, y, elevation, warp, dest_num, dest_group = image.unpack('<hh4B', values[5] + i * 8)
                assert (x, y, elevation) == (event['x'], event['y'], event['elevation']), (name, 'warp', i)
            for i, event in enumerate(source_events[2]):
                x, y, elevation, trigger, index, script = image.unpack('<hhBxHH6xQ', values[6] + i * 24)
                assert (x, y, elevation) == (event['x'], event['y'], event['elevation']), (name, 'coord', i)
                assert script == image.symbol(event.get('script', '0')), (name, 'coord script', i)
            for i, event in enumerate(source_events[3]):
                x, y, elevation, kind, payload = image.unpack('<HHBB2xQ', values[7] + i * 16)
                assert (x, y, elevation) == (event['x'], event['y'], event['elevation']), (name, 'background', i)
                if 'script' in event:
                    assert payload == image.symbol(event['script']), (name, 'background script', i)
            expected_connections = m.get('connections') or []
            if expected_connections:
                n, pointer = image.unpack('<i4xQ', connections)
                assert n == len(expected_connections), name
                for i, c in enumerate(expected_connections):
                    direction, offset, dest_group, dest_num = image.unpack('<B3xiBB2x', pointer + i * 12)
                    assert offset == c['offset'], (name, 'connection offset')
                    dest = next(source_ids[candidate] for candidate, pair in map_ids.items() if pair == (dest_group, dest_num))
                    assert dest == c['map'], (name, 'connection destination')
            else:
                assert connections == 0, name
            count += 1
    print(f'native maps: {count} headers/events/connections and {len(layouts)} layouts match upstream data')


def verify_script_tables(ref, image):
    count = 0
    for file, macro, table in [('data/script_cmd_table.inc', 'script_cmd_table_entry', 'gScriptCmdTable'),
                               ('data/specials.inc', 'def_special', 'gSpecials')]:
        entries = []
        for line in (ref / file).read_text().splitlines():
            if macro == 'script_cmd_table_entry':
                match = re.match(r'^[ \t]*script_cmd_table_entry[ \t]+\w+[ \t]+(\w+)', line)
            else:
                match = re.match(r'^[ \t]*def_special[ \t]+(\w+)', line)
            if match:
                entries.append(image.symbol(match[1]) + (0x02000000 if re.search(r'requests_effects\s*=\s*(?:1|TRUE)\b', line) else 0))
        for i, expected in enumerate(entries):
            assert image.unpack('<Q', image.symbol(table) + i * 8)[0] == expected, (table, i)
        count += len(entries)
    text = (ref / 'data/event_scripts.s').read_text()
    text += '\n' + (ref / 'data/field_effect_scripts.s').read_text()
    for table in ['gSpecialVars', 'gStdScripts', 'gFieldEffectScriptPointers']:
        tail = text.split(table + '::')[1]
        entries = []
        for line in tail.splitlines():
            if re.match(r'^[ \t]*\w+::', line) or '.include' in line:
                break
            match = re.match(r'^[ \t]*\.4byte[ \t]+(\w+)(?:[ \t]*\+[ \t]*(\d+))?', line)
            if match:
                entries.append(image.symbol(match[1]) + int(match[2] or 0))
        for i, expected in enumerate(entries):
            assert image.unpack('<Q', image.symbol(table) + i * 8)[0] == expected, (table, i)
        count += len(entries)
    print(f'native scripts: {count} command/special/variable/field-effect table pointers match source')


def verify_audio(ref, image):
    players = {'MUSIC_PLAYER_BGM': 0, 'MUSIC_PLAYER_SE1': 1, 'MUSIC_PLAYER_SE2': 2, 'MUSIC_PLAYER_SE3': 3}
    entries = re.findall(r'^\s*song (\w+), (\w+), (\d+)', (ref / 'sound/song_table.inc').read_text(), re.M)
    for i, (name, player, extra) in enumerate(entries):
        header, ms, me = image.unpack('<QHH4x', image.symbol('gSongTable') + i * 16)
        assert header % 8 == 0, f'{name}: misaligned native song header {header:#x}'
        assert (header, ms, me) == (image.symbol(name), players[player], int(extra)), name
        source_path = ref / 'sound/songs/midi' / (name + '.s')
        if not source_path.exists():
            assert name == 'dummy_song_header', name
            continue
        source = source_path.read_text()
        aliases = dict(re.findall(r'\.equ\s+(\w+),\s*(\w+)', source))
        tail = source.split('\n' + name + ':')[1]
        pointers = re.findall(r'\.word\s+(\w+)', tail)
        tracks, blocks, priority, reverb = image.unpack('<4B', header)
        assert len(pointers) == tracks + 1, name
        for j, pointer in enumerate(pointers):
            target = aliases.get(pointer, pointer)
            assert image.unpack('<Q', header + 8 + j * 8)[0] == image.symbol(target), (name, target)
    print(f'native audio: {len(entries)} song-table entries and headers match upstream data')


def main():
    ref, binary = map(pathlib.Path, sys.argv[1:])
    image = Image(binary)
    verify_maps(ref, image)
    verify_audio(ref, image)
    verify_script_tables(ref, image)


if __name__ == '__main__':
    main()
