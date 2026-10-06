"""Fixture setup restores fresh copies and rejects invalid manifests."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

TOOLS = pathlib.Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from save_archive import archive


class RestoreCheckpointTest(unittest.TestCase):
    def test_regenerates_modified_outputs_and_leaves_archives_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            data = bytes(range(256)) * 512
            state = {'party': [], 'money': 3000}
            source = root / 'checkpoint.json'
            source.write_text(json.dumps(archive(data, 'played.sav', state)))
            original = source.read_bytes()
            manifest = {'schema_version': 1, 'checkpoints': [{
                'id': 'checkpoint', 'archive': source.name,
                'sha256': hashlib.sha256(data).hexdigest(), 'expected': state}]}
            index = root / 'manifest.json'
            index.write_text(json.dumps(manifest))
            output = root / 'build/checkpoint-saves'
            def run():
                return subprocess.run([sys.executable, str(TOOLS / 'restore_checkpoints.py'), str(index), str(output)], capture_output=True)
            self.assertEqual(run().returncode, 0)
            save = output / 'checkpoint/pkmemerald.sav'
            self.assertEqual(save.read_bytes(), data)
            save.write_bytes(b'changed by previous game run')
            self.assertEqual(run().returncode, 0)
            self.assertEqual(save.read_bytes(), data)
            self.assertEqual(source.read_bytes(), original)
            for key, bad in [('sha256', '0' * 64), ('id', '../escape'), ('archive', '../checkpoint.json')]:
                old = manifest['checkpoints'][0][key]
                manifest['checkpoints'][0][key] = bad
                index.write_text(json.dumps(manifest))
                self.assertNotEqual(run().returncode, 0)
                self.assertEqual(save.read_bytes(), data)
                manifest['checkpoints'][0][key] = old


if __name__ == '__main__':
    unittest.main()
