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
