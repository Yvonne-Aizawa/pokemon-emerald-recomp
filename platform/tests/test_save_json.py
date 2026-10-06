"""Check report identity and overwrite safeguards without requiring a game save."""
import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest

path = pathlib.Path(__file__).resolve().parents[1] / 'tools/inspect_save.py'
spec = importlib.util.spec_from_file_location('inspect_save', path)
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


class JsonExportTest(unittest.TestCase):
    def test_export_records_source_identity_and_preserves_state(self):
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / 'checkpoint.sav'
            target = pathlib.Path(folder) / 'reports/checkpoint.json'
            source.write_bytes(b'fixture identity')
            state = {'flags': {'81': ['FLAG_SET_WALL_CLOCK']}, 'party': []}
            inspector.export_report(target, source, state)
            report = json.loads(target.read_text())
            self.assertEqual(report['format'], 'pkmemerald-save-inspection')
            self.assertEqual(report['schema_version'], 1)
            self.assertEqual(report['state'], state)
            self.assertEqual(report['source']['sha256'], hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertEqual(report['source']['size_bytes'], len(b'fixture identity'))
            with self.assertRaises(FileExistsError):
                inspector.export_report(source, source, state)
            self.assertEqual(source.read_bytes(), b'fixture identity')
            old = target.read_bytes()
            with self.assertRaises(FileExistsError):
                inspector.export_report(target, source, {})
            self.assertEqual(target.read_bytes(), old)

    def test_diff_includes_cleared_flags_and_zeroed_variables(self):
        before = {'flags': {'81': ['FLAG_SET_WALL_CLOCK']}, 'vars': {'16384': {'value': 1, 'names': []}}}
        after = {'flags': {}, 'vars': {}}
        diff = inspector.compare(before, after)
        self.assertIsNone(diff['flags']['81']['after'])
        self.assertIsNone(diff['vars']['16384']['after'])


if __name__ == '__main__':
    unittest.main()
