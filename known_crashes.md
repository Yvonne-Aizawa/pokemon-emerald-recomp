# Known crashes

The latest 64-bit sanitizer scan (2026-10-08) ran every ctest entry,
including the whole upstream battle suite (5,539 tests), with no sanitizer
reports. Nothing is open.

## Fixed

### Trainer spotting the player crashed on 64-bit
In the 64-bit build, a trainer that noticed the player crashed in
`StringExpandPlaceholders` (via `ShowTrainerIntroSpeech`) with a NULL string.
The `trainerbattle` script command's arguments are bytecode with four-byte
pointers (`event.inc`), but `battle_setup.c` copied them straight into
`TrainerBattleParameter`, whose pointer fields are eight bytes on a 64-bit
host: the intro text, defeat text and script pointers came out wrong, and the
end-of-arguments pointer landed 32 bytes too far. `battle_setup.c.patch`
decodes the 40 argument bytes field by field (the same struct as before on
32-bit, which a static assert checks), for trainer A and for the second
trainer. The other casts of this struct only read its first four bytes, whose
layout is the same. The upstream battle tests never run trainer scripts, so
`upstream-host-regressions` now runs the host regression tests, including one
that decodes Route 102 Calvin's `trainerbattle_single`.

### Move animations reading past `gSineTable`
A complete 64-bit ASan/UBSan battle scan found three animations indexing the
320-entry sine table out of bounds. Techno Blast, Pollen Puff and Photon
Geyser sparks reuse `AnimZapCannonSpark` with start angles of 256-288, which
it only wraps after the first `Sin`/`Cos`; Poltergeist's item calls `Cos(256)`
on its last frame; Extrasensory's distortion (`gBattleAnimArgs[0] == 1`)
indexes `gSineTable` from 192 up to 320. The GBA reads neighbouring ROM data
there. `trig.c.patch` wraps the angle in `Sin` and `Cos`, and
`battle_anim_psychic.c.patch` wraps Extrasensory's index; indexes 256-319
already repeat 0-63, so in-range results are unchanged.

### Sanitizer reports skipped the rest of a test shard
ASan and UBSan end the process with `exit(1)` after a report, not a signal.
`host_test_runner.c` took any normal exit as the shard having finished, so it
neither reported CRASH nor restarted: the remaining tests of that shard never
ran (407 of 5,539 battle tests in one 64-bit sanitizer scan) while the summary
showed 0 failed. The child now sets a `finished` flag in `HostTest_Exit`; any
other exit is handled like a crash. `upstream-runner-exit` checks it, and the
upstream battle tests now get `UBSAN_OPTIONS=print_stacktrace=1` like the rest.

### Moonlight end-fade hang from a miscounted patch hunk
`Move Animations work 1`, `3` and `4` hung (killed after 60 s) on 32- and
64-bit hosts. The hand-edited hunk in `battle_anim_effects_1.c.patch` had six
new lines but an `@@` header claiming five, so `patch` silently stopped after
five: the `BeginNormalPaletteFade` call was dropped, and the `if (d != 0xFF)`
guard captured the `gTasks[taskId].func` assignment instead. When the
green-sparkle palette wasn't loaded, `AnimTask_MoonlightEndFade` re-ran every
frame and never finished. The patch was regenerated with `diff -u`; a missing
palette tag now just leaves its bit out of the fade mask (on the GBA a shift by
0xFF gives 0). `tools/check_patches.sh` now fails any patch whose hunk line
counts don't match its headers, and covers `platform/patches/test/` too;
`battle_anim_effects_3.c.patch` and `pokemon.c.patch` had harmless miscounts
(trailing context lines only) and were regenerated, with identical output.

### Evolution tracking, recorded actions, and animation assets
Evolution tracking now passes a four-byte value to the four-byte monster-data
setter. Recorded-action cleanup checks for an empty buffer before decrementing
its position. Gust palette rotation skips an unavailable palette while still
advancing the task's lifetime, avoiding invalid palette indexes and hangs.
Eight short battle-animation sprite palettes now have explicit 16-color
allocations with zero-filled tails. All 381 linked palettes in this family
have at least 32 bytes; larger multi-palette arrays keep their original size.

The capture ball-data test failed specifically for Heavy Ball because the
default full-health, level-50 target and missing-badge penalty rounded its
capture odds down to zero. The regression now uses a level-1 target at 1 HP,
so every ball can catch it and the test checks the stored ball identity.
Gameplay capture calculations are unchanged by this test correction.

Regular 64-bit, sanitizer 64-bit, and 32-bit builds succeed. Eight focused
suites pass 95 tests under ASan/UBSan and on 32-bit, covering evolution,
capture, Mega Evolution, Hurricane, Protect/Feint, Trump Card, Eject Pack,
and two explicit empty-record/missing-palette regressions. Hurricane also
passes all four checks with drawing enabled. The broader scan no longer
reports this batch's blockers, but finds the open reports above.

### Animation loops, pledge ordering, critical odds, and terrain cleanup
Ordinary and affine animation loops check the command index before looking
back one command. Pledge ordering copies only populated entries from its
small temporary turn-order array. AI critical damage rejects blocked critical
hits and keeps Gen 1 thresholds out of the later-generation odds table.
Teraform Zero skips terrain removal when only weather was active, preventing
an invalid terrain-message index.

Electro Ball's zero modified target Speed was reproduced as division by zero;
it now selects maximum power without dividing. Early terrain removal clears
its timer, preventing the reproduced ghost terrain-expiration message.
Regression tests cover these cases, including the animation-loop siblings.

Both regular 64-bit and 32-bit builds succeed. Focused 64-bit ASan/UBSan
checks pass 80 tests; selected 32-bit compatibility suites pass 69 tests.
The full front-animation drawing check passes, and runner checks report
14 passes and 9 expected failures. ARM/native data ABI verification passes;
semantic assembly patches are applied to both sides of that comparison.

### Short monster palettes, zero catch odds, and battle turn boundaries
Monster palette loads always copy 16 colors, but 76 normal/shiny palettes
contained fewer entries. Host preprocessing now declares these arrays with
16 entries, zero-filling missing colors. All 2,883 linked monster palettes
have 32-byte allocations.

Zero capture odds now return a zero shake threshold before dividing.
The Dynamax message script passes Boolean constants to a byte-pointer
comparison; the host handles that one-byte immediate comparison explicitly.
Experience masks normalize trainer flank bits to indexes 0 and 1, in both
the lookup and update paths. Turn completion avoids reading a nonexistent
next action, and the dispatcher finishes the turn if gimmick processing
consumes its final action.

The Dynamax HP test also now retains its baseline damage in static storage;
later parameters previously read an automatic local without initializing it.
Focused Ball Fetch, Commander, Dynamax, and experience checks complete with
137 passes, no failures, and 4 TODOs under 64-bit ASan/UBSan and on 32-bit.

### Native battle command operands and item action tables
Battle commands retain four-byte pointer operands and two-byte ability/species
operands in assembly. Native command views now read those encoded widths,
including the function address prepended to native calls, then widen pointers
when consuming them. Animation commands with no argument pointer use a zero
argument. Item-use and Safari action pointer tables now use native pointer
width, while their scripts retain fixed-width operands. Turn completion also
checks for the end of the battler order before reading the next action.
Recorded player, partner, and opponent controllers mask the gimmick flag
before indexing their four move slots. All five Adaptability/Tera checks pass
under ASan after this correction.

The 64-bit sanitizer runner checks complete with 14 passes and 9 expected
failures. Its low-address coroutine stack requires disabling ASan's fake stack
for battle captures; intentional-crash checks also disable ASan's SIGSEGV
handler so the runner can resume them.

### 64-bit overworld, scripts, and save compatibility
The full 64-bit suite crashed loading maps, running script commands, and
starting field effects. Assembly map headers/layouts/events/connections and
script/special/field-effect pointer tables still used GBA pointer widths and
padding. The host data adapter now emits native metadata while retaining
32-bit pointers inside encoded scripts. The ABI verifier checks unchanged
bytecode against ARM output and independently validates native C offsets and
linked map/audio metadata against upstream JSON and assembly.

Raw saves also used 32-bit `ObjectEventTemplate` and `EnigmaBerry` layouts.
A copy map generated from both checked save schemas translates complete raw
save slots to native flash on import and back on export, preserving counters,
sector rotation, and special sectors. Damaged slots remain untouched. Native
JSON pointer fields avoid shifts by 64 when validating their range, and raw
export rejects pointer truncation. Reviewed checkpoint state and byte-for-byte
raw/JSON round trips pass.

### 64-bit song playback fails and cries crash
After repairing player initialization, `test_m4a_engine` produced no music
and crashed starting a cry. Song-table entries and song headers still held
32-bit assembly pointers while C expected native pointers; voice records also
had a 12-byte ROM stride that differed from native `ToneData`. The assembly
adapter now widens song metadata on 64-bit hosts while keeping command jumps
32-bit. ROM voices retain their original format and are decoded into native
runtime voices, including drum kits, key splits, and cries.

### 64-bit boot crash in SoundMainBTM
The 64-bit boot test crashed at frame 0 during `m4aSoundInit`: the assembly
music-player table contains 32-bit pointers, which native C read as invalid
64-bit pointers. The host now supplies a native C table and correctly sized
track buffers on 64-bit builds. Player initialization clears the full native
struct, and track initialization clears through `cmdPtr` rather than a fixed
64-byte prefix, preserving the script pointers.

### Memory used after a screen frees it (found by a search, not playtesting)
After two crashes of this kind on leaving a contest, upstream's 70 source
files that free memory and change screens were searched. These fixes come
from reading the code, not from crashes seen in play. The pattern: a screen
frees its data and sets the pointer to NULL, but code still runs that frame
(later tasks in the same `RunTasks` pass, the rest of the CB2, sprite
callbacks, the old V-blank callback) or on the next screen's first frame and
reads through it. The GBA reads BIOS junk at address 0 and ignores writes;
the host crashes.
- **Slot machine, on every exit** (`slot_machine.c.patch`):
  `SlotTask_FreeDataStructures` frees `sSlotMachine`, then the same frame's
  `AnimateSprites` (reel symbols, coin digits) and `SlotMachine_VBlankCB` read
  it. The V-blank callback, sprites and tasks are now reset before the free,
  as the field does on its first frame.
- **Roulette, on every exit** (`roulette.c.patch`): `Task_ExitRoulette` frees
  `sRoulette` inside `CB2_Roulette`'s `RunTasks`, and `CB2_Roulette` then reads
  `sRoulette->flashUtil`. It now returns if `sRoulette` is NULL.
- **Berry Blender, on stopping or playing again** (`berry_blender.c.patch`):
  `CB2_CheckPlayAgainLocal`/`Link` free `sBerryBlender` in their switch, then
  read it, run the player arrow sprites and leave `VBlankCB_BerryBlender` set,
  which all read it. With it NULL they now clear the V-blank callback and skip
  the rest of the frame (the screen is black; the next screen resets sprites).
- **Trades without a trade evolution** (`trade.c.patch`): the trade animation
  frees `sTradeAnim` with `VBlankCB_TradeAnim` still set, and that frame's
  V-blank reads it in `SetTradeGpuRegs`. The callback now skips it when NULL.
- **Battle Dome tourney tree** (`battle_dome.c.patch`): choosing a trainer
  wrote `sInfoCard->pos` before the card was allocated (NULL since the last
  card closed), and closing a card left the scroll-arrow sprites alive for a
  frame, reading the freed `sInfoCard`. Both are now skipped when it is NULL.
- **Catching a new species after opening the Pokédex** (`pokedex.c.patch`):
  `Task_ClosePokedex` frees `sPokedexView` without NULLing it, and the
  caught-mon page (`Task_ExitCaughtMonPage`, an expansion addition) freed it
  again: a double free, heap corruption on the GBA too. The page now only
  forgets the pointer. (Not NULLed in `Task_ClosePokedex`: the Pokédex
  sprites still read it that frame.)
- **Berry Crush, on quitting** (`berry_crush.c.patch`, wireless only, not
  reachable yet): `MainTask` ran `UpdateGame(sGame)` after `Cmd_Quit` freed it.

### Leaving the contest results screen
Found while playtesting (SIGSEGV in `Task_FlashStarsAndHearts` as the
results screen faded back to the field); fixed by
`platform/patches/contest_util.c.patch`. The same pattern as leaving a
contest, below, on the next screen: `Task_EndShowContestResults` frees
`sContestResults` (`FreeContestResults` sets it to NULL), and the
stars-and-hearts flashing task (priority 20) runs after it in the same
`RunTasks` pass and writes `sContestResults->data->pointsFlashing`. The
screen reset the tasks when it started, so its tasks are dropped before the
free. `CB2_ShowContestResults` also copied BGs 1 and 2 to VRAM after
`RunTasks`, from the tilemap buffers just freed; it now stops there when the
results are gone.

### Leaving a contest
Found while playtesting (SIGSEGV in `Task_FlashJudgeAttentionEye` as the
contest faded back to the field); fixed by `platform/patches/contest.c.patch`.
`Task_ContestReturnToField` frees the contest's memory
(`FreeContestResources`, which sets `gContestResources` to NULL), but the
contest's other tasks stay alive and run after it in the same `RunTasks`
pass. The judge attention eye task (priority 30) reads
`eContest.prevTurnOrder` through that NULL pointer whenever a contestant's
eye is still flashing: BIOS junk on the GBA, a crash on the host. The field
resets all tasks on its first frame (`ResumeMap`), and the contest reset
them when it started, so every task left is a contest task; they are now
reset before the memory is freed.

### Saving when a script asks (Pokémon Center upstairs, Battle Frontier, ...)
Found while playtesting (going upstairs in a Pokémon Center to trade, which
asks to save first); fixed by `platform/patches/start_menu.c.patch`.
Scripts save with `special SaveGame` (`Common_EventScript_SaveGame`: the
Cable Club's trade, battle, Record Corner and Union Room receptionists, the
Battle Frontier lobbies, Trainer Hill, secret bases, the Berry Blender),
which starts the same save dialog as the start menu, and its first step,
`SaveConfirmSaveCallback`, clears the start menu's window. Started by a
script, the start menu isn't open and its window is `WINDOW_NONE`, so this
cleared `gWindows[0xFF]`, far past the 32-entry window table, filling
whatever "pixel buffer" it found there: memory corruption on the GBA too,
and a crash on the host when that junk pointer was invalid (it depends on
what lies there, so not every time). The window is now only cleared if the
start menu is open, as `RemoveStartMenuWindow` already checks.

### Opening the map in the Frontier Pass
Found while playtesting; fixed by `platform/patches/frontier_pass.c.patch`.
The Frontier map screen (`InitFrontierMap`) sets the pass's V-blank
callback, `VBlankCB_FrontierPass`, but the pass has freed its graphics
(`sPassGfx`, `HideFrontierPass`) before showing the map, so every frame on
the map read `sPassGfx->zooming` through NULL. The GBA reads BIOS junk, which
at most sets BG2's affine registers, unused by the map's text-mode
background; the host crashed as soon as the map opened. Without the pass's
graphics, the callback now skips the zoom (sprites and palettes as before).

### The debug menu's trainer selection, on a map without trainers
Found while playtesting with upstream's debug menu; fixed by
`platform/patches/debug.c.patch`. `GetTrainerIdFromLocalId` (debug.c) read
`gMapHeader.events->objectEvents[localId - 1]` and parsed its script, and
the selection tries local IDs before checking them against the map's
object count: on a map with no objects, an object past the list and a
garbage script pointer. A local ID outside the map's objects, or an object
without a script, is now "not a trainer".

### Entering the Battle Dome lobby
Found by visiting every map (`--visit-maps`, PLAN.md, Phase 19c); fixed by
`platform/patches/battle_gimmick.c.patch`. The lobby's on-resume script
(`dome_initresultstree`, `InitRandomTourneyTreeResults`) fills a random
tournament tree when the Dome has no results yet, so on the player's first
visit. To decide the simulated winners, it compares party Pokémon's types
through battle code (`CalcPartyMonTypeEffectivenessMultiplier`, down to
`GetActiveGimmick`), which reads `gBattleStruct`: NULL outside a battle.
The GBA reads BIOS junk through NULL; the host crashed on entering the
lobby. `GetActiveGimmick` now returns `GIMMICK_NONE` when there is no
battle; in battle nothing changes.

### Moves that shake the screen (Rock Slide, Ancient Power, Snore and others)
Found by upstream's move animation tests after unrelated changes;
fixed by `platform/patches/battle_anim_normal.c.patch`.
`AnimShakeMonOrBattlePlatforms` keeps a pointer (to `gBattle_BG3_X/Y` or
`gSpriteCoordOffsetX/Y`) as two halves in the sprite's signed `data`, and
rebuilt it by ORing in the signed low half: a low half of 0x8000 or more
sign-extends and sets the top 16 bits. On the GBA those variables are in
IWRAM (`0x0300xxxx`, 32 KiB), where the low half never gets that high; on
the host they are wherever the linker puts them, and once `gBattle_BG3_Y`
moved to `0x0A35BB52` the animation wrote through `0xFFFFBB52`: a crash in
the game too, whichever build's layout does that. The low half is now read
as unsigned, as `SetCallbackToStoredInData6` already does.

### Moving an object that isn't on the map (`applymovement`)
Found by visiting every map under UBSan (the Battle Dome corridor's script
moves an attendant who isn't there when warped in); fixed by
`platform/patches/scrcmd.c.patch`. For an object that isn't on the map,
`GetObjectEventIdByLocalId` returns `OBJECT_EVENTS_COUNT`, and
`ScrCmd_applymovement` and `ScrCmd_applymovementat` (expansion's follower
and overworld-Pokémon handling) read and wrote the entry one past the end of
`gObjectEvents`: on the GBA, whatever follows it in memory. Such an object is
now left alone, as `ScriptMovement_StartObjectMovementScript` already does;
objects that are there are handled as before.

### Uncompressed tilesets (secret bases, Cable Club)
Found by visiting every map under AddressSanitizer (a secret base);
fixed by `platform/patches/fieldmap.c.patch` and
`platform/patches/tilesets.c.patch`. `CopyTilesetToVram` copies an
uncompressed tileset's whole VRAM slot (all of the secondary tiles, say),
but the secret base and Cable Club images are smaller (83 tiles for a
secret base), so it read past them into whatever follows. The GBA copies
that ROM data into tiles the map never uses. Only the image is copied now;
`Host_GetUncompressedTilesetSize` (tilesets.c, where the arrays' sizes are
known) gives its size.

### The map name popup's frame (underwater maps)
Found by visiting every map under AddressSanitizer; fixed by
`platform/patches/map_name_popup.c.patch`. The popup loads 0x400 bytes (32
tiles) of frame graphics from a 960-byte (30-tile) image, so 64 bytes past
it: into the next theme's image, and past the table for the last theme. The
frame only uses its 30 tiles, so only those are loaded now.
### Closing the fly map (Fly, and the debug menu's "Fly to map")
Found while playtesting with upstream's debug menu; fixed by
`platform/patches/region_map.c.patch`. `CB2_FlyMap` runs the fly map's
callback and then animates its sprites. On the frame the map closes, the
callback (`CB_ExitFlyMap`) frees `sFlyMap` and sets it to NULL, but the
destination icons are still there, and their callback
(`SpriteCB_FlyDestIcon`) read `sFlyMap->regionMap` through NULL: BIOS junk
on the GBA, where the screen is already black by then; a crash on the host,
whenever the player flew or closed the fly map. The icons now stay as they
are on that last frame.

### The animations of Spark, Bolt Beak, Overdrive and other electric moves
Found by upstream's move animation tests on the host (PLAN.md, Phase
19b-4; "Move Animations work 1" and "4", "Z-Moves animations work" in
`test/battle/move_animations/all_anims.c`); fixed by
`platform/patches/battle_anim_electric.c.patch`. The flashing electric
sparks (`AnimSparkElectricityFlashing`) toggle their visibility when
`data[7] % data[4]` is 0, and the sparks of Spark, Bolt Beak, Electro
Drift, Overdrive and Supercell Slam pass 0 as that interval
(`CreateSparks`, `BoltBeakSparks`), so it was a modulo by zero. On the GBA,
libgcc's `__aeabi_idivmod` returns without trapping and leaves 0 as the
remainder, so the sparks flash every frame; on x86 it raises SIGFPE, in the
game too, whenever one of these moves is animated. An interval of 0 now
flashes every frame, as on the GBA.

### The animations of Shadow Force and Phantom Force
Found by upstream's move animation tests on the host ("Move Animations work
2" and "3"); fixed by `platform/patches/m4a.c.patch`. Both scripts play
their sound with `playsewithpan SOUND_PAN_ATTACKER, SOUND_PAN_ATTACKER`,
the pan in place of the sound effect, so `m4aSongNumStart` was asked for
song 0xFFC0 and read `gSongTable` far past its end: on the GBA, whatever
follows the table in ROM; on the host, past its memory (SIGSEGV). The
`m4aSongNum*` functions now ignore song numbers past the table
(`LAST_PHONEME_SONG`), so these moves play no sound there.

### An AI trainer choosing whom to revive with Revival Blessing
Found by upstream's battle tests on the host (PLAN.md, Phase 19b-4; "AI
revives the best fainted ally with Revival Blessing" in
`test/battle/ai/ai_switching.c`); fixed by
`platform/patches/battle_ai_switch.c.patch`. `AI_SelectRevivalBlessingMon`
scores every party slot with 0 HP, and empty slots have 0 HP too, so with
fewer than 6 Pokémon it set up an empty slot as a switch-in candidate, and
`GetHealthPercentage` divided by its max HP of 0. The GBA's division
returns without trapping (and the hazards check that follows then skips
the slot); on x86 it raises SIGFPE, in the game too, whenever an AI
trainer with fewer than 6 Pokémon uses Revival Blessing. Empty slots and eggs are now skipped, as
`GetFirstFaintedPartyIndex` does; the AI's choice is the same.

### A double battle against two trainers
Found by upstream's battle tests on the host (PLAN.md, Phase 19b-4; the
two-opponent tests in `test/battle/ability/commander.c` and
`test/battle/ability/illusion.c`); fixed by
`platform/patches/party_menu.c.patch`. At the start of every battle,
`BufferBattlePartyOrderBySide` lists each side's party order: in a double
battle, the two battlers' Pokémon first and then the rest. Against two
trainers both opponents have party index 0 (each trainer uses their own
half of the party), so only one slot was skipped and the loop wrote a
seventh entry past its 6-byte stack array: harmless on the GBA, an abort
from the host's stack protector (in the game too, e.g. when two trainers
spot the player at once). The loop now stops at the end of the array; the
entries it keeps are the same.

### Time-of-day palette blending outdoors (shift by 32)
Found by the sanitizer build's `monkey-boot-101` run (UBSan: "shift exponent
32 is too large" at `field_weather.c:545`); fixed by
`platform/patches/field_weather.c.patch`. `ApplyColorMap` builds the mask
of palettes to time-blend as `(1 << numPalettes) - 1`, and the weather code
calls it with all 32 palettes. Shifting a 32-bit value by 32 is undefined:
the GBA's shifter gives 0, so the mask covers every palette, but x86 masks
the count to 0, so the mask was empty and no palette was time-blended. The
mask is now all ones for 32 palettes. Not a crash on its own, but the
sanitizer build stops on it.

### Collecting a Day Care egg whose parents share a move
Found by upstream's tests on the host (PLAN.md, Phase 19b-2; three tests in
`test/daycare.c`); fixed by `platform/patches/daycare.c.patch`. When both
parents know the same move, `GiveParentSharedLevelUpMoves` looks it up in the
baby's level-up learnset, but its inner loop advanced `i` instead of `j`, so
it read further and further past its 4-move array until stack junk matched
a move (on the GBA, then adding that junk move to the egg; on the host, a
SIGSEGV off the top of the stack). The loop now advances `j`, as intended.

### Battle text outside a battle (Illusion lookup)
Found by upstream's tests on the host (Phase 19b-2; `test/text.c` expands
every battle string with no battle running); fixed in
`platform/patches/battle_util.c.patch`. A string with a Pokémon's name asks
`GetIllusionMonPtr` whether an Illusion hides it, which read and wrote
through `gBattleStruct` while it is NULL (BIOS memory on the GBA). With no
battle it now returns NULL (no Illusion). No path to this in the game itself
is known.

### Battle animations without an argument (Disguise, Shell Trap, Z-Moves, ...)
Found by upstream's battle tests on the host (PLAN.md, Phase 19b-3; the
Disguise test in `test/battle/move_effect/absorb.c`); fixed by
`platform/patches/battle_script_commands.c.patch`. Several battle scripts
play an animation with a `NULL` argument (`playanimation BS_ATTACKER,
B_ANIM_ZMOVE_ACTIVATE, NULL`: Disguise, Shell Trap and Beak Blast setup,
Salt Cure, Z-Moves, totem auras, held item effects, the Safari Zone
Pokéblock throw), and `PlayAnimation` read the argument through that
pointer (BIOS memory on the GBA; the animations ignore the value). It now
passes 0 for a `NULL` argument.

### Hex, Venoshock and similar moves against a statused target
Found by upstream's battle tests on the host (Phase 19b-3; `test/battle/
move_effect/double_power_on_arg_status.c`); fixed by
`platform/patches/battle_util.c.patch`. When the target has the status that
doubles the move's power, `CalcMoveBasePower` checks the move's first
additional effect, but moves like Hex have none, and it read through their
`NULL` effect list (BIOS memory on the GBA). It now checks the count first.

### Dynamaxing
Found by upstream's battle tests on the host (Phase 19b-3; the Dynamax
tests in `test/battle/move_effect/`); fixed by
`platform/patches/battle_script_commands.c.patch`. `BattleScript_DynamaxBegins`
uses `jumpifbyteequal B_SHOW_DYNAMAX_MESSAGE, FALSE, ...`, which takes two
addresses, with two constants, so `jumpifarrayequal` compared the bytes at
address 0 (NULL) with each other. On the GBA they are the same BIOS byte, so
it always jumps, skipping the extra message as the default config intends.
An array now always equals itself without being read. (With
`B_SHOW_DYNAMAX_MESSAGE` set to TRUE it would still read addresses 0 and 1;
that's upstream's bug, unchanged.)

### Throwing a ball that can't catch (capture odds 0)
Found by upstream's battle tests on the host (Phase 19b-3; `test/battle/
move_effect/autotomize.c`, a Heavy Ball on a light Pokémon); fixed by
`platform/patches/battle_script_commands.c.patch`. `ComputeBallShakeOdds`
divides by the capture odds, which can be 0 (SIGFPE on x86). The GBA's
division (libgcc's `__aeabi_uidiv`) returns 0xFFFFFFFF there instead, and the
patch gives that value.

### Closing the trainer card
Found by the sanitizer monkey runs (PLAN.md, Phase 19a); fixed by
`platform/patches/trainer_card.c.patch`. `CloseTrainerCard` frees `sData`,
but the card's V-blank callback stays installed until the next screen sets
its own, and in between `BlinkTimeColon` wrote through the NULL `sData`
(SIGSEGV; on the GBA the write lands in read-only BIOS memory). The callback
now skips its `sData` work once `sData` is gone.

### Memory errors found by the sanitizers (no crash seen)
Reads the GBA tolerates, found by AddressSanitizer/UBSan in the monkey runs
(PLAN.md, Phase 19a). None crashed in play, but each reads memory it
shouldn't and could crash with a different memory layout:
- `pokemon.c.patch`: `GetBattlerPartyStateByPokemon` returned a pointer into
  `gBattleStruct` while it is NULL (outside battles; the overworld calls it
  every frame). Callers check for NULL, but got a non-NULL invalid pointer,
  and one path reads through it.
- `field_door.c.patch`: drawing 1x2 and 2x2 doors read past the door's
  palette list (for the empty upper-layer tiles).
- `quickstart.c.patch`: the title screen's quick-start label loaded a
  16-colour palette from a 6-colour array.
- `region_map.c.patch`: the Hoenn region map loaded 3 palettes from a
  2-palette array.
- `menu.c.patch`: compressed map tilesets were copied to VRAM at the size of
  their whole VRAM slot (e.g. 16 KiB) instead of their decompressed size,
  reading heap memory past the buffer.
- Not a bug, exempted: the "smol" tANS decoders (`decompress.c.patch`) read a
  word past the end of a compressed image when refilling their bit buffer;
  the bits are unused.

### Heap misuse found by the sanitizers' heap checks (no crash seen)
Found once the game's own heap marks its unused memory off-limits
(`malloc.c.patch`). They behave the same on the GBA (the heap is a plain
array on both), but read memory that may hold something else by then:
- `dma3_manager.c.patch` + `malloc.c.patch`: heap blocks were freed while a
  DMA3 copy from them was still queued (e.g. `CopyWindowToVram`, then the
  window removed before the V-blank). Freeing a block now moves pending
  copies from it to a snapshot of its bytes, so they copy exactly what the
  GBA would. Test: `test_dma_free`.
- `item_icon.c.patch`: item icon sprites kept pointing at their template in
  a freed temporary buffer (upstream notes this in `FreeSpriteTiles`); each
  now keeps a copy of its template.
- `text.c.patch`: a glyph ending at a window's right edge on its last row
  read and wrote back the 4 bytes past the window's buffer.
- `decompress.c.patch`: `LoadCompressedSpriteSheet` loaded a sheet's declared
  size even when its graphics decompress to less (the wall clock's hands);
  the buffer now has the declared size, zero-filled.

### Opening the PC in a Pokémon Center
Fixed by `platform/patches/pokemon_storage_system.c.patch`, three upstream
reads/writes through bad pointers that the GBA tolerates:
- `CreateDisplayMonSprite` loaded a palette from `displayMonPalette` before it
  was ever set (uninitialized heap memory).
- `SetMovingMonPriority` wrote through a NULL `movingMonSprite` whenever the
  cursor reached the buttons/party/box with no Pokémon held (crashed on
  opening "Party Pokémon").
- Leaving the box (B → No, or Close Box) frees `sStorage` while that frame's
  sprite callbacks still read through it (`SpriteCB_CursorShadow`).

### Opening the Pokédex
Fixed by `platform/patches/pokedex.c.patch`, two upstream out-of-bounds reads:
- `CreatePokedexList` looks up one entry past the Hoenn dex (`HOENN_DEX_COUNT`
  counts `HOENN_DEX_NONE`), whose national number is 0; `GetSetPokedexFlag(0)`
  then read ~512 MB past the save block. Dex number 0 now reads as unseen.
- `UpdateSelectedMonSpriteId` indexed `gSprites[0xFFFF]` before checking for
  "no sprite" (crashed on opening an entry); the check now comes first.
- Staying on an entry: the info screen stops "the current cry" through
  `gMPlay_PokemonCry`, which is NULL until a cry has played. The real sound
  engine ignores that; the silent stand-in (`platform/src/host_m4a.c`) wrote
  through it. Its player functions now treat NULL as a no-op too.

### Throwing a Poké Ball
Fixed by `platform/patches/item_use.c.patch`: using an item from the battle bag
calls `CannotUseItemsInBattle(item, NULL)`, which reads the HP of that NULL
Pokémon. It now uses the active battler's party slot instead.

### Nicknaming a caught Pokémon
Fixed in `platform/patches/naming_screen.c.patch`: after naming a caught
Pokémon the naming screen frees its data and returns straight to the battle,
whose sprite update keeps running the naming screen's cursor and underscore
callbacks, which read through the freed pointer. They now do nothing once it
is gone.

### Learning a new move when the Pokémon already knows 4
Fixed by `platform/patches/sprite.c.patch`: the summary screen's move
category icon (also used by the move relearner and the HGSS Pokédex) has a
`NULL` first animation, for `DAMAGE_CATEGORY_NONE`, which is never played.
A new sprite starts on animation 0, though, so creating it read its first
frame through `NULL` (BIOS junk on the GBA, replaced at once by
`StartSpriteAnim`). `SetSpriteSheetFrameTileNum` now skips a `NULL` animation.
Also crashed when selecting a move on the summary's moves page.

### Printing a battle message
Fixed by `platform/patches/line_break.c.patch` (crashed in `BuildNewString`,
fault address 0x2, under `BattleStringExpandPlaceholders`). Automatic line
breaking first estimates how many lines a message needs, allocates that many,
then lays the words out. The layout can use fewer lines than the estimate. The
unused lines keep `numWords == 0` and an unset `words` pointer (`Alloc` doesn't
zero), and `BuildNewString` read the first word's length through it. On the
GBA that reads junk, and the last real line writes a line break over the
string's EOS. `BuildNewString` now skips trailing lines that have no words.
Regression test: `platform/tests/test_line_break.c`.

### A Pokémon's "vertical shake" animation ending
Fixed by `platform/patches/pokemon_animation.c.patch` (SIGFPE in
`VerticalShakeTwice`, seen in the overworld). `sVerticalShakeData` ends with
the row `{-1, 0}`, and `VerticalShakeTwice` and `VerticalShakeLowTwice`
compute the shake height as `... / var6` before checking for that end marker,
so the last frame divided by 0. The GBA's software division doesn't trap and
the result is unused there; x86's `idiv` raises SIGFPE. Only unoptimised
(Debug) builds crashed: at -O2 GCC moves the division into the branch that
uses it. Both functions now skip the division when `var6` is 0. Regression
test: `platform/tests/test_mon_anim_vshake.c` (it shows the crash only in a
Debug build).

### Native 64-bit heap and save-block alignment
UBSan reported misaligned `MemBlock` and `SaveBlock1` accesses in the native
64-bit build. The GBA allocator rounded sizes to four bytes, leaving split
headers and returned pointers misaligned for eight-byte native pointers.
`malloc.c.patch` now aligns the heap and allocation sizes to the block header's
native alignment. `load_save.c.patch` preserves native alignment when randomly
relocating saves and pads the heap scratch copy between SaveBlock2 and
SaveBlock1. The original four-byte behavior remains on 32-bit builds.
`test_heap_asan` checks odd allocation sizes, save relocation, and data retained
through `MoveSaveBlocks_ResetHeap` under the sanitizers.

### Native 64-bit song headers and upstream test records
UBSan found song headers aligned to four bytes despite containing native
eight-byte pointers. `host_data_asm.py` now aligns the exported song header
before its label; encoded track commands stay unchanged. The linked-data ABI
check also verifies header alignment.

The native upstream test runner read null strings because GCC placed 40-byte
`Test` records at 32-byte boundaries, leaving gaps that the runner interpreted
as records. CMake's host copies of `test.h` and `battle.h` now explicitly align
these records to pointer size, making the linker-collected array contiguous.

### Native task overlays, compile-time RNG checks, and max-level battle bars
The native sanitizer suite found pointer arrays and cursor structs overlaid on
unaligned task halfwords. Union Room pointers also overlapped the link-group
halfwords on 64-bit hosts. `union_room.c.patch` and `list_menu.c.patch` keep
pointer-bearing state in native arrays indexed by task ID, including the
outline cursor that exceeds the task-data buffer on 64-bit hosts.

The daycare RNG path exposed upstream's `if_comptime` null-dereference trick
inside `__builtin_constant_p` to UBSan. The host copy of `metaprogram.h` uses
`__builtin_choose_expr` to preserve compile-time branching without that trick.

The battle display read experience-table entry 101 for a level-100 Pokémon.
`battle_interface.c.patch` avoids the nonexistent entry and supplies a nonzero
range to the bar math; the renderer still hides max-level experience progress.
The intentional-crash runner check disables ASan's SIGSEGV handler for that
check so the parent can observe and resume the expected crash.

### Short egg nickname and Mega indicator palette buffers
After the earlier sanitizer blockers were removed, daycare tests read beyond
the four-byte Japanese egg nickname: `MON_DATA_NICKNAME` copies a full fixed
name field. `daycare.c.patch` pads this constant to the required name length.
Battle tests also exposed a 15-entry Mega indicator palette loaded as a full
16-entry sprite palette. `battle_gimmick.c.patch` supplies a zero-padded local
palette before loading it, without reading beyond the generated asset.
