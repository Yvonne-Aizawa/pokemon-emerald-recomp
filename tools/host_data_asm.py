#!/usr/bin/env python3
"""Adapt native map/audio/pointer tables for 64-bit C; preserve bytecode words."""
import re
import sys

mode = sys.argv[1]
lines = sys.stdin.readlines()
if mode == 'song':
    exports = set()
    header = False
    first_pointer = False
    for line in lines:
        match = re.match(r'\s*\.global\s+(\w+)', line)
        if match:
            exports.add(match[1])
        match = re.match(r'\s*(\w+):\s*$', line)
        if match and match[1] in exports:
            print('\t.p2align 3')  # native SongHeader contains eight-byte pointers
            header = True
            first_pointer = True
        if header and re.match(r'\s*\.(word|4byte)\s', line):
            if first_pointer:
                print('\t.space 4')  # pointer alignment after four header bytes
                first_pointer = False
            line = re.sub(r'\.(word|4byte)\b', '.8byte', line)
        sys.stdout.write(line)
elif len(sys.argv) > 2 and sys.argv[2] == 'data/field_effect_scripts.s':
    table = False
    for line in lines:
        match = re.match(r'\s*(\w+):(?::)?\s*$', line)
        if match:
            table = match[1] == 'gFieldEffectScriptPointers'
            if table:
                print('\t.p2align 3')
        if table:
            line = line.replace('.4byte', '.8byte')
        sys.stdout.write(line)
elif len(sys.argv) > 2 and sys.argv[2] == 'data/event_scripts.s':
    macro = ''
    table = False
    for line in lines:
        match = re.match(r'\s*\.macro\s+(\w+)', line)
        if match:
            macro = match[1]
        match = re.match(r'\s*(\w+):(?::)?\s*$', line)
        if match:
            table = match[1] in ('gSpecialVars', 'gStdScripts')
            if table:
                print('\t.p2align 3')
        if table or macro in ('script_cmd_table_entry', 'def_special'):
            line = line.replace('.4byte', '.8byte')
        if re.match(r'\s*\.endm', line):
            macro = ''
        sys.stdout.write(line)
elif len(sys.argv) > 2 and sys.argv[2] == 'data/maps.s':
    # Generated map tables: symbolic words are pointers; numeric words are
    # dimensions/counts. Native structs need eight-byte alignment/tail padding.
    label = ''
    header = False
    connection_count = False
    for line in lines:
        match = re.match(r'\s*(\w+):(?::)?\s*$', line)
        if match:
            if label.endswith('_Layout') or (label and header):
                print('\t.space 4')
            label = match[1]
            header = False
            connection_count = label.endswith('_MapConnections')
            print('\t.p2align 3')
        match = re.match(r'(\s*)\.4byte\s+(.+)', line)
        if match:
            value = match[2].strip()
            if re.fullmatch(r'[A-Za-z_]\w*', value):
                if connection_count:
                    print('\t.space 4')
                    connection_count = False
                line = line.replace('.4byte', '.8byte')
                if value.endswith('_Layout') and not label.startswith('gMap'):
                    header = True
            elif value == '0' and (header or label.startswith('gMap')):
                line = line.replace('.4byte', '.8byte')
        sys.stdout.write(line)
    if label.endswith('_Layout') or (label and header):
        print('\t.space 4')
elif len(sys.argv) > 2 and sys.argv[2] == 'data/map_events.s':
    macro = ''
    for line in lines:
        match = re.match(r'\s*\.macro\s+(\w+)', line)
        if match:
            macro = match[1]
        if macro in ('object_event', 'coord_event', 'bg_event', 'map_events'):
            if '.4byte' in line:
                if macro == 'map_events':
                    print('\t.space 4')
                line = line.replace('.4byte', '.8byte')
            if macro == 'coord_event' and re.match(r'\s*\.space 2\b', line):
                line = line.replace('.space 2', '.space 6')
        if macro == 'clone_event':
            line = line.replace('.space 8', '.space 12')
        if re.match(r'\s*\.endm', line):
            macro = ''
        # The input's .align 2 suffices for packed object templates, but event
        # tables and coord/background arrays require native pointer alignment.
        if re.match(r'\s*\w+:(?::)?\s*$', line):
            print('\t.p2align 3')
        sys.stdout.write(line)
else:
    song_macro = False
    for line in lines:
        if re.match(r'\s*\.macro\s+song\s', line):
            song_macro = True
        if song_macro:
            line = line.replace('.4byte', '.8byte')
            if re.match(r'\s*\.endm\b', line):
                print('\t.space 4')  # native struct Song tail padding
                song_macro = False
        sys.stdout.write(line)
