"""Continuing a game saved on a map without object events (known_crashes.md).

Underwater Route 128 has no NPCs, so its object event table is NULL, and
LoadSaveblockObjEventScripts once read scripts from it for every slot. This
moves a played checkpoint there, then presses A on the title screen and
Continue on the main menu.
"""
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

output_dir, binary = map(pathlib.Path, sys.argv[1:])
work = pathlib.Path(tempfile.mkdtemp(prefix='pkmemerald-continue-'))
FRAMES = 2400

try:
    subprocess.run([str(binary.resolve()), '--convert-save',
                    str(output_dir / '04-after-picking-starter' / 'pkmemerald.sav'), str(work / 'pkmemerald.json')],
                   check=True, capture_output=True, text=True)
    document = json.loads((work / 'pkmemerald.json').read_text())
    block = document['game']['blocks']['save_block_1']
    block['location'].update(mapGroup=0, mapNum=53, warpId=-1, x=38, y=19)  # MAP_UNDERWATER_ROUTE128
    block['pos'].update(x=38, y=19)
    block['mapLayoutId'] = 53  # LAYOUT_UNDERWATER_ROUTE128
    (work / 'pkmemerald.json').write_text(json.dumps(document))

    result = subprocess.run([str(binary.resolve()), '--fast', '-f', str(FRAMES), '-s', str(work),
                             '-i', '600:a,1300+10:a,1600+10:a'], capture_output=True, text=True)
    assert result.returncode == 0 and f'ran {FRAMES} frames' in result.stdout, result.stdout + result.stderr
    print('continued a game saved on Underwater Route 128')
finally:
    shutil.rmtree(work, ignore_errors=True)
