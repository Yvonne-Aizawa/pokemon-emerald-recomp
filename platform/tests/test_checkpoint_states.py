"""Actual game loader state must match the reviewed checkpoint projection."""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from inspect_save import inspect

manifest_path, output_dir, binary = map(pathlib.Path, sys.argv[1:])
manifest = json.loads(manifest_path.read_text())
for entry in manifest['checkpoints']:
    expected = json.loads((manifest_path.parent / entry['archive']).read_text())['inspection']['state']
    actual = inspect(binary, output_dir / entry['id'] / 'pkmemerald.sav')
    assert actual == expected, f"{entry['id']}: game loader state differs from reviewed archive"
    print(entry['id'], 'game state matches and source hash unchanged')
