"""Long file lines must preserve frame, hold, button tokens and comments."""
import pathlib
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    missing_save = root / 'missing.sav'
    def accepts(text):
        script = root / 'input.txt'
        script.write_text(text)
        # Inspection exits after argument parsing, before any game startup.
        result = subprocess.run([binary, '-i', '@' + str(script), '--inspect-save', str(missing_save)],
                                capture_output=True, text=True)
        assert result.returncode == 1 and 'inspect:' in result.stderr, result.stderr
    # Put every part of a valid step across the 511-byte fgets boundary.
    token = '123456+12345:select|right'
    for split in range(1, len(token)):
        accepts(',' * (511 - split) + token)
    accepts('0:a,' * 200 + '123456+12345:select\n')
    accepts('#' + 'comment ' * 200 + '\n123456:select\n')
    accepts('123456:select # comment\n234567:right')
print('script file: chunk boundaries preserve valid tokens and comments')
