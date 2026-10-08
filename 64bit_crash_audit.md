# 64-bit crash audit — fixes to do

Reviewed 2026-10-08 by three parallel code audits covering pointer handling,
binary layouts, and memory safety. These are static findings, not reproduced
crashes. Existing platform patches and `known_crashes.md` fixes were considered.
The first two findings have since been fixed in the host patch set.

Upstream sources under `reference/` must remain unchanged. Implement upstream
changes through `platform/patches/`, and document completed crash fixes in
`known_crashes.md` with appropriate verification.

## Priority 1: dynamic menus

- [x] **Use the actual native ListMenu storage when setting initial selection.**
  - Location: `reference/src/script_menu.c:423-424`,
    `DrawMultichoiceMenuDynamic`.
  - The caller casts `gTasks[listTaskId].data` to `struct ListMenu *`, but
    `platform/patches/list_menu.c.patch` moves 64-bit list state into
    `sHostListMenus`. The caller therefore passes unrelated task bytes to
    `ListMenuChangeSelectionFull`.
  - Trigger: a dynamic menu with a nonzero initial selection. A zero initial
    selection can return before the problematic selection traversal.
  - Proposed fix: expose/use a task-ID-based list-menu operation or accessor
    that resolves the real storage; audit other external task-data casts.
  - Implemented: `ListMenuChangeSelectionFullByTask` resolves storage through
    the same `LIST_MENU_DATA` mapping used by the other list-menu APIs, and the
    dynamic menu calls it with the list task ID.
  - Verification: open dynamic menus with zero and nonzero initial selections,
    including scrolling and selection callbacks, on 64-bit ASan/UBSan and 32-bit.
  - Confidence: high.

- [x] **Reconstruct the complete items pointer during menu cleanup.**
  - Location: `reference/src/script_menu.c:542,559-560`,
    `Task_HandleScrollingMultichoiceInput`.
  - `LoadWordFromTwoHalfwords` writes only four bytes through `(u32 *)&items`.
    The upper half of the uninitialized native pointer remains garbage before
    `FreeListMenuItems` dereferences/frees it.
  - Trigger: accepting or canceling a scrolling multichoice.
  - Proposed fix: decode into a `u32` temporary, then explicitly convert through
    `uintptr_t` to the pointer type. This remains compatible with the current
    low-address game heap.
  - Implemented: cleanup now decodes into `itemsAddress` and converts that
    value to `struct ListMenuItem *` through `uintptr_t`.
  - Verification: accept and cancel menus repeatedly under 64-bit sanitizers;
    check that pointer reconstruction clears the upper bits independently of
    previous stack contents. Check 32-bit behavior too.
  - Confidence: high; actual failure depends on stack contents.

## Priority 2: field effects and trainer movement

- [ ] **Reconstruct complete indoor/outdoor field-move callbacks.**
  - Locations: `reference/src/field_effect.c:3069,3081,3198,3211`.
  - The outdoor and indoor show-Pokémon effects decode four bytes directly
    into uninitialized native `IntrCallback` locals. VBlank handlers call these
    addresses immediately; end handlers install them for subsequent calls.
  - Trigger: the field-move show-Pokémon banner, indoors or outdoors.
  - Proposed fix: decode to `u32` first and explicitly convert to the callback
    type using the host's existing low-address representation.
  - Verification: exercise both banner variants through animation and cleanup
    under 64-bit sanitizers, and verify 32-bit compatibility.
  - Confidence: high; actual failure depends on stack contents.

- [ ] **Reconstruct the complete buried-trainer object pointer.**
  - Location: `reference/src/trainer_see.c:945`,
    `Task_SetBuriedTrainerMovement`.
  - Four bytes are written into an uninitialized native `objEvent` pointer,
    which is then used by movement functions.
  - Trigger: revealing a `MOVEMENT_TYPE_BURIED` trainer through
    `MovementAction_RevealTrainer_Step0`.
  - Proposed fix: decode to `u32`, then convert through `uintptr_t`.
  - Verification: run a buried-trainer reveal through completion on 64-bit
    sanitizers and 32-bit.
  - Confidence: high; actual failure depends on stack contents.

## Priority 3: latent movement and selection paths

- [ ] **Reconstruct the complete levitating-object pointer.**
  - Location: `reference/src/event_object_movement.c:11281-11282`,
    `ApplyLevitateMovement`.
  - Four bytes are written into an uninitialized native `objectEvent` pointer,
    followed immediately by a read of `objectEvent->spriteId`.
  - Trigger: the `levitate` movement action. The identified Sootopolis script
    is marked unused, so ordinary-game reachability is uncertain.
  - Proposed fix: decode to `u32`, then convert through `uintptr_t`. Also
    remove or correct the unused pointer reconstruction in
    `DestroyLevitateMovementTask` at line 11298.
  - Verification: explicitly start, advance, and stop levitation on 64-bit
    sanitizers and 32-bit.
  - Confidence: high technical confidence; dormant identified trigger.

- [ ] **Stop leaking allocations when filtering boxed Pokémon for evolution.**
  - Location: `reference/src/chooseboxmon.c:117-121`,
    `ChooseBoxMon_CanEvolve`.
  - A Pokémon is allocated before the boxed-mon check. That branch returns
    without freeing it. Storage icon filtering calls this for populated slots
    (`pokemon_storage_system.c:4461-4467`). Repeated box browsing can exhaust
    the fixed game heap; allocation failure terminates through `fatalf`.
  - Trigger: storage selection with `SELECT_PC_MON_EVOLUTION`. No checked-in
    game script was found enabling this mode; a custom/debug caller is needed.
  - Proposed fix: perform the rejection before allocation, or free on every
    return path.
  - Verification: repeatedly browse populated boxes in this selection mode
    and confirm stable heap usage.
  - Confidence: high for leak and termination mechanism, latent reachability.
    This affects both architectures; native allocator overhead increases
    memory pressure on 64-bit.

## Priority 4: dormant mystery-event compatibility

- [ ] **Decode Enigma Berry event payloads using their encoded layout.**
  - Locations: `reference/src/berry.c:2317-2325`, `SetEnigmaBerry`, called by
    `reference/src/mystery_event_script.c:228-236`.
  - The native 64-bit EnigmaBerry structure is 64 bytes, while the GBA payload
    is 52 bytes. A native-size raw copy can overread the payload and misdecode
    widened/repositioned description pointers. The native checksum also no
    longer describes the original payload layout.
  - Proposed fix: validate payload bounds/checksum in the encoded layout and
    translate fields explicitly, including the intended description-pointer
    semantics. Reuse save-layout translation logic where appropriate.
  - Verification: import valid, truncated, and malformed encoded payloads;
    verify rejection or correct descriptions without out-of-bounds reads.
  - Confidence: medium as a crash candidate. Ordinary payloads likely fail
    checksum validation first. Invalid descriptions could be dereferenced by
    `berry_tag_screen.c:459,465` if validation succeeds. The legacy mystery
    event menu call is commented out, and link receiving is unavailable.

- [ ] **Remove or guard mystery-event input-pointer truncation.**
  - Location: `reference/src/mystery_event_script.c:62`; reconstructed
    addresses are used at lines 214,222,282,319,363,383,396.
  - The input script address is stored in a `u32`. A future caller providing a
    stack or libc-allocated buffer above 4 GB would lose the upper address bits
    when relocated operands are reconstructed.
  - Proposed fix: keep the native script base in pointer-width runtime state
    and perform relocation using encoded offsets, or explicitly reject
    unsupported input addresses until this path is migrated.
  - Verification: exercise an input buffer above 4 GB as well as the current
    low-address game-heap input.
  - Confidence: high conditional defect; current receive buffers use the low
    game heap and avoid it. Not an established normal-game regression.

## Shared constraints and exclusions

`LoadWordFromTwoHalfwords` (`reference/src/util.c:93-95`) is a four-byte integer
decoder. Do not change it to write eight bytes globally: legitimate callers
pass actual `u32` destinations. Fix pointer callers or add a typed host helper.

The current Linux build deliberately places executable code/static data in a
low address range, and the game allocator uses a static heap. Consequently,
ordinary four-byte pointer storage is not automatically a current crash.
Preserve that requirement while applying the focused fixes above. Removing
it requires a separate migration of runtime pointer storage and encoded script
references to native pointers, handles, or offsets.

The slot-machine callback reconstruction was excluded from the active findings:
its destination allocation is zeroed, so the upper pointer bits currently stay
zero. It is still a candidate for consistent pointer-decoding cleanup.

Passing the existing test suite does not establish that these paths are safe;
the findings need targeted exercises of their triggers.

The first two fixes are implemented in `platform/patches/list_menu.h.patch`,
`platform/patches/list_menu_api.c.patch`, and
`platform/patches/script_menu.c.patch`. `tools/check_patches.sh` reports all
three new patches applying exactly; the repository has four unrelated
pre-existing inexact patches. Runtime tests have not yet been run.
