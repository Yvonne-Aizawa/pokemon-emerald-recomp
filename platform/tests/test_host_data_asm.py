#!/usr/bin/env python3
"""Song metadata uses native pointers without changing encoded command jumps."""
import pathlib
import subprocess
import sys
import unittest

TOOL = pathlib.Path(__file__).resolve().parents[2] / 'tools' / 'host_data_asm.py'


def transform(mode, source, path=""):
    return subprocess.check_output([sys.executable, str(TOOL), mode, path],
                                   input=source, text=True)


class DataAssemblyTest(unittest.TestCase):
    def test_song_header_preserves_bytecode_jump(self):
        source = ('.global song\ntrack:\n.byte GOTO\n.4byte track\n'
                  'song:\n.byte 1, 0, 0, 0\n.4byte voices\n.4byte track\n')
        output = transform('song', source)
        self.assertIn('\t.p2align 3\nsong:', output)
        self.assertIn('.byte GOTO\n.4byte track\n', output)
        self.assertIn('.byte 1, 0, 0, 0\n\t.space 4\n.8byte voices\n.8byte track\n', output)

    def test_song_table_preserves_voice_records(self):
        source = ('.macro song label, ms, me\n.4byte \\label\n'
                  '.2byte \\ms\n.2byte \\me\n.endm\n'
                  '.macro voice sample\n.4byte \\sample\n.endm\n')
        output = transform('data', source)
        self.assertIn('.8byte \\label\n', output)
        self.assertIn('.2byte \\me\n\t.space 4\n.endm', output)
        self.assertIn('.macro voice sample\n.4byte \\sample\n.endm', output)

    def test_null_map_slots_keep_native_pointer_stride(self):
        output = transform('data', 'gMapLayouts:\n.4byte Town_Layout\n.4byte 0\n', 'data/maps.s')
        self.assertIn('.8byte Town_Layout\n.8byte 0\n', output)

    def test_script_table_widening_preserves_script_arguments(self):
        source = ('.macro script_cmd_table_entry constant, value\n.4byte \\value\n.endm\n'
                  'gStdScripts:\n.4byte script\ngStdScripts_End:\n'
                  'script:\n.byte 4\n.4byte target\n')
        output = transform('data', source, 'data/event_scripts.s')
        self.assertIn('.8byte \\value', output)
        self.assertIn('gStdScripts:\n.8byte script', output)
        self.assertIn('script:\n.byte 4\n.4byte target', output)

    def test_battle_script_keeps_fixed_width_operands(self):
        source = ('.macro jumpifstatus battler, flags, jumpInstr\n'
                  '.byte 1\n.4byte \\flags\n.4byte \\jumpInstr\n.endm\n')
        output = transform('data', source, 'data/battle_scripts_1.s')
        self.assertIn('.4byte \\flags', output)
        self.assertIn('.4byte \\jumpInstr', output)

    def test_battle_action_tables_use_native_pointers(self):
        source = ('gBattlescriptsForUsingItem::\n.4byte heal\n'
                  'gBattlescriptsForSafariActions::\n.4byte watch\n'
                  'heal:\n.byte 4\n.4byte target\n')
        output = transform('data', source, 'data/battle_scripts_2.s')
        self.assertIn('.8byte heal', output)
        self.assertIn('.8byte watch', output)
        self.assertIn('heal:\n.byte 4\n.4byte target', output)

    def test_coord_event_pointer_alignment(self):
        source = '.macro coord_event script\n.2byte 1, 2\n.byte 3\n.space 1\n.2byte 4, 5\n.space 2\n.4byte \\script\n.endm\n'
        output = transform('data', source, 'data/map_events.s')
        self.assertIn('.space 6\n.8byte \\script', output)


if __name__ == '__main__':
    unittest.main()
