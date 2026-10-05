# Known crashes

None open.

## Fixed

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
