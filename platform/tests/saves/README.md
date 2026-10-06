# Played save checkpoints

Collect these through the normal SAVE menu. Do not set flags, add items, or
construct saves in code. Start a separate playthrough with a dedicated save
folder, so the personal save stays separate:

```sh
PKM_FIXED_TIME="2026-01-01 12:00:00" build/pkmemerald -s /tmp/pkm-checkpoint-playthrough
```

Keep that clock value for this playthrough. The RTC advances at 60 frames per
second while running and restarts at the given time each launch; for collection,
prefer a continuous session. Record any restarts in the manifest. On Windows,
set the same environment variable before launching the Windows executable.

1. `00-truck`: enter the truck after Birch's introduction and save through the menu.
2. `01-clock`: set the bedroom clock, finish the dialogue, save once free to move.
3. `02-rival`: meet the rival in their house, finish the scene, save before Route 101.
4. `03-starter`: rescue Birch, receive your starter in the lab, finish the scene, save.
5. `04-pokedex`: battle the rival and return to the lab for the Pokédex; save afterward.
6. `05-woods`: reach Route 104/Petalburg Woods with 2–3 party members and some items.
7. `06-badge`: beat Roxanne; finish the immediate dialogue and save outside battle.

If the menu is unavailable at a checkpoint, continue until it becomes available
and record the actual location/events. Close or pause the game after SAVE finishes before copying
`/tmp/pkm-checkpoint-playthrough/pkmemerald.sav` to a separately named checkpoint.
Don't commit unreviewed saves. Reviewed full JSON checkpoints are bundled in
`platform/tests/fixtures/checkpoints/`; local `.sav` files stay ignored.

Inspect with the build matching the save's upstream version:

```sh
python3 platform/tools/inspect_save.py /path/to/01-clock.sav
python3 platform/tools/inspect_save.py /path/to/01-clock.sav --compare /path/to/02-rival.sav
# Windows: supply --binary build-win/pkmemerald.exe (run on Windows).
```

The wrapper verifies SHA-256 before/after inspection. The native inspector opens
only for reading, disables flash persistence, and exits before normal startup
creates config files or logs. Reports use numeric map/species/item IDs and raw
player-name bytes; flags and variables include compiler-evaluated upstream names.
Absent flags are clear; absent variables are zero. Bad-egg fields expose party
checksum failures. The game's loader checks save-sector integrity and may select
the older valid slot if the newest save is damaged, as normal gameplay does.
Reports do not validate every map, item, or story-state invariant. Review the
flags/badges, story variables, location and party against what actually happened.

For each approved fixture, record checkpoint, game/upstream commit IDs, clock,
restarts, SHA-256, location, party, badges and relevant events. Later regression
runs must copy fixtures into temporary directories before running the game.
Save files and completed builds must not be uploaded as CI artifacts.

## Export JSON inspection reports

Export one checkpoint, or a group, using the native inspector from the matching
build. Original saves remain untouched; existing output files are refused.

```sh
python3 platform/tools/inspect_save.py test-saves/00-truck.sav -o /tmp/truck-report.json
python3 platform/tools/inspect_save.py test-saves/*.sav --output-dir /tmp/checkpoint-reports
```

Each report has `format`, `schema_version`, source filename/size/SHA-256, and
`state` containing the decoded inspection fields. Reports are pretty-printed
with stable key ordering for reading and diffing. Raw name bytes and numeric
map/species/item IDs preserve the values used by this build. These are inspection
projections, not lossless save conversions: storage, all event objects and other
unreported data are omitted, and JSON cannot be converted back into a game save.
Use the original `.sav` as the regression fixture. The source hash associates
its JSON report with that exact input. JSON reports are local outputs, not CI
artifacts.

## Lossless JSON flash archives

`save_archive.py` is separate from the inspection-report exporter above. It
preserves every byte of the 128 KiB flash image in 32 ordered Base64 sectors,
including both save slots, Pokémon storage, Hall of Fame, special sectors,
padding and erased space. It requires only Python's standard library; it can
archive even a blank or corrupt flash image without claiming it is playable.

```sh
python3 platform/tools/save_archive.py export test-saves/00-truck.sav -o /tmp/truck-full.json
python3 platform/tools/save_archive.py import /tmp/truck-full.json -o /tmp/truck-restored.sav
# Optional readable state from a matching local game build:
python3 platform/tools/save_archive.py export test-saves/00-truck.sav -o /tmp/truck-readable-full.json --binary build/pkmemerald
```

The output directory must exist. Existing files and symlinks are never
overwritten. Original inputs are opened for reading only. The optional native
inspector runs on a temporary snapshot of exactly the bytes archived.

Format `pkmemerald-flash-archive`, schema version 1, contains source filename,
size and SHA-256 plus geometry and all ordered flash sectors. Each sector has
its own SHA-256. Import validates format/version, geometry, sector order,
Base64, lengths and hashes before writing any output. Duplicate JSON keys are
rejected; input archives are limited to 1 MiB. An unchanged archive restores a
byte-for-byte identical `.sav`, preserving existing encryption, save counters
and game checksums instead of recomputing them. No game assets are needed for
raw export/import.

Decoded inspection fields are a read-only projection. When included they have
a separate hash, and modified projections are refused so changes cannot be
silently discarded. This version supports archival round trips, not editing
flags/party/items or repairing corrupted saves; hand-recomputing hashes does
not establish game-state validity. The older inspection-only JSON format
cannot be imported as a full save. Keep full archives local alongside the
played checkpoints; `test-saves/` remains ignored and no archives are uploaded
by CI.
