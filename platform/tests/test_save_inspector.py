"""Malformed input must be rejected without touching the input or its directory."""
import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    samples = [('short', b'not a save'), ('empty', b'\xff' * 131072)]
    damaged = bytearray(b'\xff' * 131072)
    struct.pack_into('<HHII', damaged, 4096 - 12, 14, 0, 0x08012025, 1)
    samples.append(('bad-sector-id', damaged))
    for name, data in samples:
        path = root / (name + '.sav')
        path.write_bytes(data)
        digest = hashlib.sha256(data).digest()
        result = subprocess.run([binary, '--inspect-save', str(path)], capture_output=True)
        assert result.returncode == 1, (name, result.returncode, result.stderr)
        assert hashlib.sha256(path.read_bytes()).digest() == digest, name
        assert not result.stdout, (name, result.stdout)
    result = subprocess.run([binary, '--inspect-save', str(root / 'missing.sav')], capture_output=True)
    assert result.returncode == 1
    assert sorted(p.name for p in root.iterdir()) == sorted(name + '.sav' for name, _ in samples)
print('save inspector: malformed files rejected and unchanged')
