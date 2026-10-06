"""Lossless flash restoration and rejection of incomplete or damaged archives."""
import copy
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

TOOLS = pathlib.Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location('save_archive', TOOLS / 'save_archive.py')
archive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)


class SaveArchiveTest(unittest.TestCase):
    def setUp(self):
        # All byte values, sectors and their unused bytes must survive.
        self.data = bytes(range(256)) * 512
        self.doc = archive.archive(self.data, 'checkpoint.sav', {'flags': {}, 'party': []})

    def test_round_trip_including_erased_and_non_game_images(self):
        for data in (self.data, b'\xff' * archive.FLASH_SIZE, b'\0' * archive.FLASH_SIZE):
            doc = json.loads(json.dumps(archive.archive(data, 'save.sav')))
            self.assertEqual(archive.restore(doc), data)

    def test_rejects_missing_duplicate_reordered_and_damaged_sectors(self):
        for change in ('missing', 'duplicate', 'reordered', 'data', 'hash', 'whole_hash', 'geometry', 'version', 'inspection'):
            doc = copy.deepcopy(self.doc)
            sectors = doc['flash']['sectors']
            if change == 'missing': sectors.pop()
            elif change == 'duplicate': sectors[1] = copy.deepcopy(sectors[0])
            elif change == 'reordered': sectors.reverse()
            elif change == 'data': sectors[0]['data'] = '!' + sectors[0]['data'][1:]
            elif change == 'hash': sectors[0]['sha256'] = '0' * 64
            elif change == 'whole_hash': doc['source']['sha256'] = '0' * 64
            elif change == 'geometry': doc['flash']['sector_size'] = 2048
            elif change == 'version': doc['schema_version'] = 2
            elif change == 'inspection': doc['inspection']['state']['party'] = [1]
            with self.subTest(change=change), self.assertRaises(ValueError):
                archive.restore(doc)

    def test_rejects_short_save_and_inspection_only_report(self):
        with self.assertRaises(ValueError): archive.archive(b'short', 'save.sav')
        with self.assertRaises(ValueError): archive.restore({'format': 'pkmemerald-save-inspection'})

    def test_cli_roundtrip_and_existing_output_protection(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            source, report, restored = root / 'source.sav', root / 'archive.json', root / 'restored.sav'
            source.write_bytes(self.data)
            def run(*args):
                return subprocess.run([sys.executable, str(TOOLS / 'save_archive.py'), *map(str, args)], capture_output=True)
            self.assertEqual(run('export', source, '-o', report).returncode, 0)
            self.assertEqual(run('import', report, '-o', restored).returncode, 0)
            self.assertEqual(restored.read_bytes(), self.data)
            self.assertNotEqual(run('import', report, '-o', source).returncode, 0)
            self.assertEqual(source.read_bytes(), self.data)
            report.write_text('{"format":"x","format":"y"}')
            self.assertNotEqual(run('import', report, '-o', root / 'invalid.sav').returncode, 0)
            self.assertFalse((root / 'invalid.sav').exists())


if __name__ == '__main__':
    unittest.main()
