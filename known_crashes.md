# Known crashes

None open.

## Fixed

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
