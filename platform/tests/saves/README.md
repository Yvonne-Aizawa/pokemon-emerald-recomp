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
Don't commit unreviewed saves. No checkpoints are bundled yet.

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
