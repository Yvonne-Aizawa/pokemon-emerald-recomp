# Port Plan: `reference/` (pokeemerald-expansion) → PC game

## Current status (2026-10-06)

The game is playable from boot to the overworld and battles, with graphics, input, saves and sound, on Linux and Windows (32-bit builds; Windows cross-compiled with MinGW). It has a settings file (window, fullscreen, sound, keyboard layout), and CI builds and tests both platforms on every pull request. The project is source only: no builds are published. Playtesting so far covers the intro (Birch's speech), the truck, Littleroot, Route 101, wild battles, catching and nicknaming, learning moves, the Pokémon Center PC and the Pokédex. Crashes found while playing are fixed with patches documented in [known_crashes.md](known_crashes.md); none are open.

| Phase | Status |
|------:|--------|
| 0–6   | Done |
| 7     | Done (no separate work needed; see its status note) |
| 8     | Done |
| 9     | Not needed |
| 10–11 | Done |
| 12    | Done (no separate work needed) |
| 13    | Done (checked by ear and against mGBA) |
| 14–15 | In progress: verified by playtesting so far, continuing as the game is played further |
| 16    | Not started (optional) |
| 17    | Done (Linux and Windows builds, source only); on real Windows still to check: sound, `--console`/log file |
| 18    | Not started (64-bit build; needed for macOS) |
| 19    | In progress: 19a done (sanitizers + monkey runs in CI, heap checks included); 19b-1, 19b-3 and the CI split done; 19b-2 done (non-battle tests); 19b-4 in progress (abilities and hold effects done); GBA save compatibility dropped (see 19b, *save layout*); **next**: the rest of 19b-4, 19b-5, then 19c (every map); 19a-2's checkpoint monkey runs on hold |


## Source under analysis

- `reference/` is a git submodule of [`Yvonne-Aizawa/pokeemerald-expansion`](https://github.com/Yvonne-Aizawa/pokeemerald-expansion), a fork of [`rh-hideout/pokeemerald-expansion`](https://github.com/rh-hideout/pokeemerald-expansion) (a GBA ROM hack base, not a standalone game).
- 390+ C source files, 314+ headers, ~76 graphics subdirs, custom audio engine, ARM assembly.
- Targets devkitARM / `arm-none-eabi-gcc`, links a 32 MB ROM with a custom linker script (`ld_script_modern.ld`).
- Requires a Pokémon Emerald baseline ROM (sha1 `f3ae088181bf583e55daf962a92bb46f4f1d07b7`) to extract data from.
- Game logic is mostly portable C99. Platform-specific code is concentrated in:
  - `include/gba/` — hardware register definitions, memory-mapped I/O.
  - `src/main.c`, `src/crt0.s` — GBA startup + interrupt-driven main loop.
  - `libagbsyscall/` — GBA BIOS syscall wrappers.
  - `sound/MPlay*` + `src/m4a.c` — custom audio engine.
  - `src/librfu_*` + `src/link*` — link-cable multiplayer.
  - `src/agb_flash_*` — 1M flash save emulation.
  - `src/graphics.c`, `src/bg.c`, `src/window.c`, `src/sprite.c` — tile/sprite engine.
  - `Makefile`, `ld_script_modern.ld`, `data/mb_*.gba` (multiboot) — build & GBA cart layout.
  - `tools/gbagfx`, `tools/mid2agb`, `tools/wav2agb`, `tools/aif2pcm` — GBA-specific asset tooling.

## Strategy

Build a **PC Hardware Abstraction Layer (HAL)** that replaces every GBA-specific surface, and a **PC frontend** (window, main loop, renderer, audio, input, save, files) on top of SDL2/SDL3 and a C compiler the user already has. Then incrementally swap each platform-specific file.

The C game logic — battle engine, AI, scripts, party, items, overworld state machine, menus, RNG, save data structures — stays untouched except for the small handful of `IWRAM_DATA` / `EWRAM_DATA` / `ARM_FUNC` annotations and `gba/` includes.

### Target stack
- **Language/runtime**: C11, compiled with system `gcc`/`clang`/`cl`.
- **Build system**: CMake (cross-platform; replaces the GBA Makefile).
- **Windowing / input / events / audio**: SDL2 (or SDL3) + SDL2_mixer + SDL2_ttf (only if we choose to render text with it; see Phase 11).
- **Renderer**: SDL2 `SDL_Renderer` with `SDL_TEXTUREACCESS_STREAMING`, or OpenGL 3.2 core if we need shader-driven scaling.
- **Filesystem layout** (instead of a 32 MB ROM):
  ```
  pkmemerald/
    assets/        # converted graphics/audio (built from reference/data/ + reference/graphics/)
    saves/         # per-slot .sav files (instead of flash 1M)
    pak/           # drop-in data blob mirroring reference/data/
  ```
- **Tooling**: Python scripts to convert GBA `.4bpp`, `.gbapal`, `.lz`, `.rl`, `.smol`, voice-group `.s` into PNG/WAV/JSON. Reuse `reference/tools/gbagfx` and `reference/tools/preproc` as host binaries where they still help.

### Non-goals (deliberately deferred)
- **No legal/clean-room rewrite of Nintendo assets.** This port still depends on the baseline ROM and `reference/data/` blobs. Replacing art/audio with original assets is its own workstream (out of scope here).
- **No mGBA/retroarch shortcut.** The user asked for a *port*, not a wrapper.
- **No rewrite in C++/Rust.** The codebase is C99 and we keep it that way; SDL2 has a clean C API.
- **No rewritten battle engine, AI, or scripts.** Those are platform-agnostic and stay as-is.

### Edit-size discipline
Each phase is sized to fit a single focused session. Phases list the **files touched** up front; if a phase ends up touching more, it should be split. Phases later in the plan are bigger but are still broken into commits.

---

## Phase 0 — Repository layout (no porting yet)

**Goal**: Stand up a clean place for the port to live, without disturbing the upstream submodule.

**Edits**
- `PLAN.md` — this file.
- `README.md` — short note: `reference/` is the upstream we are porting, `src/` and `platform/` are the new PC port, build with CMake.
- `.gitignore` — extend with `build/`, `assets/`, `saves/`, IDE files, CMake user presets.

**Done when** `git status` is clean and the new layout is documented.

**Status (done).** `README.md` and `.gitignore` added. There is no top-level `src/`: all port code lives in `platform/` and `tools/`.

---

## Phase 1 — Build system: replace GBA Makefile with CMake

**Goal**: The existing C sources can be compiled for the host (no linking yet, no GBA toolchain). This exposes the dependency surface we have to stub.

**Edits**
- New `CMakeLists.txt` at repo root.
  - `cmake_minimum_required(VERSION 3.20)`, `project(pkmemerald C)`.
  - `set(CMAKE_C_STANDARD 11)`, `-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function`.
  - Glob `reference/src/*.c` (minus GBA-only ones — listed below) and `platform/**/*.c`.
  - Target `pkmemerald-core` (STATIC lib, no main, no `main()`).
  - Find SDL2 via `find_package(SDL2)`.
- New `platform/` directory with placeholder `platform.c`.
- Exclude from the glob for now: `crt0.s`, `libisagbprn.c`, `agb_flash_1m.c`, `agb_flash.c`, `agb_flash_le.c`, `agb_flash_mx.c`, `AgbRfu_LinkManager.c`, `libgcnmultiboot.s`, `librfu_intr.c`, `librfu_rfu.c`, `librfu_sio32id.c`, `librfu_stwi.c`, anything under `sound/`, `m4a.c`, `m4a_tables.c`, `test_runner_battle.c`, multiboot files in `data/`.
- Stub `include/gba/gba.h` is **not** in this phase — that's Phase 2.

**Done when** `cmake -S . -B build && cmake --build build` compiles `pkmemerald-core` (with at most a few hundred expected `gba/` undefined-symbol errors that Phase 2 will resolve). Don't link an executable yet.

**Status (done, as part of Phase 3).** `CMakeLists.txt` builds a 32-bit host (`-m32`) and runs upstream's own preprocessing (`preproc`, graphics conversion) instead of a separate asset pipeline. `m4a.c` and `m4a_tables.c` are no longer excluded (Phase 13).

---

## Phase 2 — HAL stub headers (`include/gba/`)

**Goal**: Every header under `reference/include/gba/` (and the few other GBA-specific headers under `include/`) compiles when included from host code. Stubs return zero/null/no-op, so we can see what the C code *actually* depends on.

**Edits**
- New `platform/include/gba/` with the same directory layout.
  - `gba.h` — pull in every sub-stub. Define `vu8/vu16/vu32/vs8/vs16/vs32` as plain `volatile` aliases.
  - `io_reg.h`, `defines.h`, `types.h`, `multiboot.h`, `isagbprint.h` — minimal type/macro shims.
  - Define `IWRAM_DATA`, `EWRAM_DATA`, `IWRAM_INIT`, `EWRAM_INIT`, `COMMON_DATA`, `ARM_FUNC`, `NOINLINE`, `ALIGNED`, `PACKED`, `UNUSED`, `USED` as empty `__attribute__`s (or compiler-portable equivalents) on host.
  - `REG_*` macros → `(volatile uint16_t*)0 /* TODO: phase 9 */`. (We keep them as volatile pointers so reads/writes don't get optimised out.)
  - `*(vu16 *)BG_PLTT`, `DISPcnt`, `BG0cnt`, `BLD*`, etc. — keep as volatile pointer to a placeholder address; the renderer phase (9) will replace with real framebuffer state.
- Add `platform/include/gba.h` umbrella that includes all of the above.
- Update `CMakeLists.txt` to add `platform/include` and `reference/include` to `target_include_directories`.

**Done when** the headers compile cleanly from a test TU that includes every GBA header. **No** source-file behaviour changes yet.

**Status (done, as part of Phase 3).** `platform/include/gba/` replaces the GBA headers; `REG_*` and the VRAM/palette/OAM macros point into host arrays (`g_host_mmio` etc.) that the renderer, sound and input code read. `test_gba_headers` covers them.

---

## Phase 3 — Compile `pkmemerald-core` (the bulk of the game)

**Goal**: Get every C file in `reference/src/` to compile, with all GBA hardware symbols resolved to the Phase 2 stubs. This is the "scary" phase because it makes the dependency surface concrete, but each file is only edited if it has a real, *non-GBA* portability bug.

**Edits**
- Re-enable one group at a time in `CMakeLists.txt`:
  1. `data/` and `event_data.c`-style pure-data files first.
  2. Core engine: `random.c`, `util.c`, `string_util.c`, `malloc.c` (the local one), `link.c` (data-side only — the SIO parts come in Phase 16).
  3. Script engine: `script_table.c`, `event_data.c`, `event_object_movement.c` (no rendering yet).
  4. Battle core: `battle_main.c`, `battle_util.c`, `battle_script_commands.c`, etc. — these are platform-agnostic.
- Fix **only** build-blocker issues that aren't GBA hardware:
  - Use of `__attribute__((target("arm")))` (the `ARM_FUNC` macro) → no-op on x86.
  - Inline assembly in `.c` files (`asm(".include ...")`) → guard with `#if defined(__arm__)`.
  - Implicit `int` return types where the C99 compiler is strict.
  - The local `mini_printf.c`/`assertf.c` — include and link.
- Stub `main.c` to expose `int host_main(int argc, char **argv)` (rename the original `AgbMain` body, which we'll delete in Phase 5 anyway — we keep the function for now so call sites still resolve).

**Done when** `cmake --build build` builds `pkmemerald-core` as a static lib. There will be thousands of warnings; that's expected and we suppress in Phase 17.

**Status (done).** All 371 non-excluded `reference/src/*.c` files compile into `pkmemerald-core`; `reference/` stays unmodified. How it ended up differing from the outline above:
- **32-bit host (`-m32`, CMake option `PKM_HOST_32BIT`, default ON).** The game keeps pointers in u32 task-data pairs (`SetWordTaskArg`, 71 sites) and static-asserts struct sizes against GBA buffers (`list_menu.c`, `recorded_battle.c`). Requires `gcc-multilib`; Phase 5 will need a 32-bit SDL2 (`libsdl2-dev:i386`).
- **Upstream's pipeline is kept:** `tools/prepare_reference.py` drives upstream make for host tools, generated headers and converted graphics; `tools/host_preprocess.sh` does `cpp | preproc` per file (preproc is needed for `_()`/`COMPOUND_STRING` charmap strings and `INCBIN`/`INCGFX` data). Re-run with `cmake --build build --target reference-prepare`.
- **Host include tree** (`build/host_include/`, symlinks) so `global.h`'s `#include "gba/gba.h"` resolves to `platform/include/gba/`.
- **Source changes go in `platform/patches/<file>.c.patch`**, applied to a copy at build time (currently only `random.c`: C `Random32` instead of ARM asm). `multiboot.c` is excluded and replaced by `platform/src/host_multiboot.c`.
- **Stub fixes:** `VRAM`/`PLTT`/`OAM`/`EWRAM`/`IWRAM` are integer addresses (as on GBA); `INTR_CHECK`/`INTR_VECTOR`/`SOUND_INFO_PTR` are lvalues; `CpuFastSet` drops its 4-byte alignment asserts (on GBA those only hold because apcs-gnu aligns every struct to 4).
- `main.c` compiles as-is; the `host_main` rename was skipped since Phase 4 excludes `main.c` anyway.
- `data/*.s` (event/battle scripts, maps) is not built yet, so linking will report those symbols as undefined.

---

## Phase 4 — Entry point, CRT, and the main loop

**Goal**: A real `main()` that boots, runs one frame, exits. No rendering, no input, no audio — just proves the platform seam works.

**Edits**
- New `platform/src/host_main.c`:
  - `int main(int argc, char **argv)`: parse `-d` (data dir), `-s` (save dir), call `Platform_Init`, then `Host_RunMainLoop` (empty for now), then `Platform_Shutdown`.
- New `platform/include/platform.h` with `Platform_Init`, `Platform_Shutdown`, `Platform_FrameBegin`, `Platform_FrameEnd`, `Platform_PollEvents`, `Platform_GetTicks`, `Platform_SleepMs`.
- New `platform/src/platform_stub.c` — empty implementations returning success.
- New `platform/src/main_loop.c` — runs at 60 Hz, calls `Host_OnVBlank()` for each tick. The game engine's "VBlank callback" gets registered here.
- Delete `reference/src/crt0.s` from the build. (Keep the file on disk so the diff stays reviewable; it's excluded in `CMakeLists.txt`.)
- Exclude `reference/src/main.c` from the build (the original GBA `AgbMain` is no longer our entry point).
- Disable interrupt registration (`InitIntrHandlers`, `gIntrTableTemplate`) by defining `HOST_BUILD` and `#ifndef HOST_BUILD` around it in callers — but only if/when those callers are pulled in. For now, just don't include `main.c`.

**Done when** `./build/pkmemerald` runs, prints "boot ok", runs 60 frames of nothing, and exits cleanly with code 0.

**Status (done).** `./build/pkmemerald` prints "boot ok", runs 60 frames in ~1.0 s and exits 0. Run all checks with `ctest --test-dir build`. Differences from the outline above:
- The existing `platform/include/platform/platform.h` and `platform/src/platform.c` (headless POSIX) were extended rather than adding `platform_stub.c`. `Platform_Init` takes a `struct PlatformConfig` (data/save dirs). New: `Platform_QuitRequested`/`Platform_RequestQuit` (SIGINT/SIGTERM set it) and `Platform_GetTimeNs`/`Platform_SleepNs`.
- The main loop (`platform/main_loop.h`) paces at the GBA's real refresh rate, 59.7275 Hz (`HOST_FRAME_NS`), not 60 Hz. It drops the backlog if it falls more than 4 frames behind.
- `pkmemerald -f N` sets the frame limit (default 60 until Phase 5 adds a window; `-f 0` runs until quit).
- `reference/src/main.c` is excluded; `gMain`, `ReadKeys`, the interrupt table etc. live there and will be undefined once game code is linked in.
- Tests: `platform/tests/test_main_loop.c`, plus a ctest `pkmemerald-boot` check of the "done when" output.

### Phase 4b — Full link: the game boots headless (done)

Added after a link check showed 97% of unresolved symbols were assembly *data*. `./build/pkmemerald` now links the whole game (`--whole-archive`, so any missing symbol is a build error), runs `AgbMain`, and plays through the copyright screen into the intro scenes with nothing drawn.
- **Assembly data is assembled for x86, not converted.** `data/*.s` and the 530 `mid2agb` songs go through `tools/host_assemble.sh`, which uses upstream's preproc/cpp pipeline plus three ARM→x86 fixups: `@` comments (keeping `\@`), `.word` → `.4byte`, `.align N` → `.p2align N`. `tools/verify_data_abi.py` (ctest `data-abi-matches-gba`, runs when `arm-none-eabi-gcc` and gdb are installed) checks the result against upstream's ARM toolchain: all 544 objects are byte-identical, with identical relocations, and the C structs read from that data (map headers/events/connections, voicegroups, ...) have identical layouts under both ABIs.
- **`main.c` is built, patched** (`platform/patches/main.c.patch`): `AgbMain` only initialises; `HostMain_RunFrame` runs one loop iteration, then dispatches the V-count and V-blank interrupts through `gIntrTable`. H-blank and serial interrupts are not emulated yet. The main loop's callback API is now `Host_SetFrameCallback`/`Host_RunFrame`.
- **Other patches:** `decompress.c` (no copying of machine code into RAM buffers; call the functions directly) and `save.c` (an erased flash sector made `CopySaveSlotData` read far out of bounds — harmless on GBA, a segfault on PC).
- **Temporary stand-ins, replaced by later phases:** `host_m4a.c` (silent audio, Phase 13), `host_flash.c` (in-memory flash, saves don't persist yet, Phase 8), `host_rfu.c` (no wireless adapter, Phase 16).
- **Permanent host versions:** `host_rtc.c` (the cartridge clock reads the PC's local time; `siirtc.c` excluded), `host_gba_misc.c` (EWRAM reset/`ReInitializeEWRAM`, ROM header, GameCube multiboot, `BitUnPack`).
- **Hardware details that mattered:** `EWRAM_DATA`/`EWRAM_INIT` now live in their own sections so RAM resets behave as on hardware; `REG_KEYINPUT` powers on as `0x03FF` (active-low — at 0 the game saw A+B+Start+Select held and soft-reset); executables link with `-no-pie`.

---

## Phase 5 — Window, events, and VBlank timing

**Goal**: A real window with a clear colour, a real event loop, real frame timing.

**Edits**
- New `platform/src/sdl2_window.c` implementing `platform.h`:
  - `Platform_Init` → `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)`, `SDL_CreateWindow("pkmemerald", 240*3, 160*3, …)`.
  - `Platform_PollEvents` → `SDL_PollEvent`; on `SDL_QUIT` set a quit flag.
  - `Platform_FrameBegin` → render clear, `SDL_RenderClear`.
  - `Platform_FrameEnd` → `SDL_RenderPresent`.
  - `Platform_GetTicks` → `SDL_GetTicks`.
  - `Platform_SleepMs` → `SDL_Delay`.
- Add `find_package(SDL2 REQUIRED)` to `CMakeLists.txt`.
- Add a new target `pkmemerald` (executable) that links `pkmemerald-core` + the platform layer.

**Done when** launching `./build/pkmemerald` opens a 720×480 window, clears it to a known colour (we use white for now; Pokémon Emerald boots to white), and closes on window close.

**Status (done; checked on an X11 desktop).** Differences from the outline above:
- `platform/src/platform_sdl2.c` replaces the headless POSIX backend (`platform.c`, removed). It owns a 240×160 `0x00RRGGBB` framebuffer (`Platform_GetFramebuffer`) shown at an integer scale with letterboxing; the window is resizable and `-x SCALE` sets its initial size (default 3 → 720×480). No vsync: the main loop paces at 59.73 Hz, sleeping with `SDL_Delay` and spinning the last ~2 ms. Closing the window or Ctrl-C quits; the default run length is now "until quit".
- `platform/src/host_render.c` is the GBA display emulation, separate from SDL. For now it draws what a GBA shows with every layer off: white during forced blank, otherwise the backdrop colour (BG palette entry 0) — the game's real palette, fades included. Phases 10–11 add BG layers and sprites on top.
- **DMA now actually transfers** (`platform/src/host_dma.c`): the stub `DmaSet` only wrote registers, so no palette/VRAM/OAM upload ever happened. Immediate transfers now run in `Host_DmaSet`; V-blank/H-blank-timed ones stay armed in the registers for Phase 10 (scanline effects) and Phase 13 (sound FIFO).
- 32-bit SDL2 (`libsdl2-dev:i386`) is required. `PKM_HOST_32BIT` now sets `-m32` before `project()` so CMake picks the i386 library directories.
- `-o FILE` saves the last frame as a PPM, for checking rendering without a display. Tests run SDL's dummy video driver.

---

## Phase 6 — Input

**Goal**: GBA key state read by the engine matches PC keyboard/gamepad.

**Edits**
- New `platform/src/input.c`:
  - Define `A_BUTTON`/`B_BUTTON`/`SELECT_BUTTON`/`START_BUTTON`/`DPAD_*`/etc. in `platform/include/keys.h` (same bit values as the GBA `keys.h`, so the engine doesn't notice).
  - Maintain a `static uint16_t gHostKeyState` updated from SDL events.
  - Implement `UpdateLinkAndCallCallbacks`-style read by exposing `Platform_GetKeyState()`.
  - The game's `ReadKeys()` (in `main.c`) is the natural site to call this. Move that body into `platform/src/input.c` and expose `Platform_ReadKeys(void)`; have the port's `main_loop.c` call it once per frame and stash the result.
- Map keyboard: Z=A, X=B, Enter=Start, Backspace=Select, arrows=D-pad, Shift=L, Ctrl=R.
- Map gamepad: button 0=A, 1=B, 6=Back=Select, 7=Start=Start, dpad/hats.

**Done when** running the game, Z/X/Enter/Backspace/arrows produce the right `gKeyStateNew`/`gKeyStateOld` bits — verify by toggling `gKeyStateNew` in a temporary log on keypress (remove the log after).

**Status (done).** Simpler than outlined, because `main.c` is now built unchanged: the game's own `ReadKeys` (key repeat, L=A option) stays, and the host only fills `REG_KEYINPUT` (active-low) before each frame.
- `platform/src/input_sdl2.c`: keyboard by physical key position (Z=A, X=B, Enter=Start, Backspace=Select, arrows, Shift=L, Ctrl=R) and any number of hot-pluggable gamepads (SDL's standard layout: A/B, Back=Select, Start, D-pad or left stick, shoulders or triggers = L/R). Key state is tracked from events and cleared when the window loses focus. Exposed as `Platform_GetButtons()` with the GBA's bit layout (`PLATFORM_BUTTON_*`).
- `platform/src/host_input.c`: `Host_SetKeypad` writes `REG_KEYINPUT`, cancelling opposite D-pad directions (impossible on real hardware).
- `platform/tests/test_input.c` injects SDL key events and checks the mapping, the register, and the game itself: pressing A during the intro skips to the title screen.

---

## Phase 7 — Memory model & section attributes

**Goal**: `IWRAM_DATA`/`EWRAM_DATA`/`COMMON_DATA` etc. are no-ops on PC, but `AGB_ASSERT`/`SOUND_INFO_PTR`/`INTR_VECTOR` (in `gba/defines.h`) need real replacements because the engine uses them in non-GBA code paths.

**Edits**
- In `platform/include/gba/defines.h`:
  - `SOUND_INFO_PTR` → a real heap pointer set in `Platform_Init`.
  - `INTR_VECTOR`/`INTR_CHECK` → removed or `#define`d to 0 in `HOST_BUILD`.
- `IWRAM_DATA`, `EWRAM_DATA`, `COMMON_DATA`, `IWRAM_INIT`, `EWRAM_INIT`, `ARM_FUNC` — confirm they're empty on host.
- For files that use `__attribute__((section("common_data")))` on huge globals, add a CMake-compile check that those compiles succeed. (Most do; we only care about catching regressions here.)
- Heap sizing: `malloc.c` exposes a fixed `HEAP_SIZE` based on EWRAM. Replace with a host-side bump allocator in `platform/src/heap.c`, sized to e.g. 64 MiB.

**Done when** running the game, internal `malloc`/`free` (the local one, not libc) works — verified by allocating a Pokémon party, freeing it, and re-allocating without corruption. A small unit-style smoke test in `host_main.c` does this on boot.

**Status (done; no separate work was needed).** The game's memory is emulated rather than replaced: IWRAM/EWRAM are host arrays, `EWRAM_DATA`/`COMMON_DATA` variables are reset like on hardware (`Host_ResetEwram`), and the game's own `malloc.c` runs unchanged on its `gHeap` (0x1C500 bytes, as on the GBA) — every screen that allocates (summary, PC, Pokédex, battles) exercises it. `SOUND_INFO_PTR`, `INTR_CHECK` and `INTR_VECTOR` are host variables (`gba/defines.h`); `INTR_VECTOR` is unused because interrupts are dispatched by the host. No bump allocator or boot-time smoke test was added.

---

## Phase 8 — Save system

**Goal**: `agb_flash_*` calls (read/write sector) become file I/O in `saves/`.

**Edits**
- New `platform/src/save_io.c`:
  - Maps "sector 0..N" onto `saves/slot{N}.sav` (raw binary, sector size = 4 KiB to mirror the real 1 Mbit flash).
  - Implements the four `agb_flash_*` sector ops as read/write/erase/commit against files.
  - On `Platform_Init`, create `saves/` if missing.
- Exclude `reference/src/agb_flash*.c` from the build. Their public API (`ReadFlash`, `ProgramFlash`, `EraseFlashSector`, etc.) gets redirected through the platform layer.
- Wire `LoadGameSave`/`SaveGameSave` to use the new file-backed sector ops.

**Done when** saving in-game writes a real file under `saves/`, exiting and re-launching restores the same state. (Cross-check with a hash of the original GBA flash sector on a known save.)

**Status (done).** One file instead of per-sector files: `saves/pkmemerald.sav` (`-s DIR` to change) is a raw 128 KiB image of the flash chip — the format GBA emulators use. (*Correction, Phase 19b-2:* the host's save blocks are laid out differently from the GBA build's, so a GBA emulator's save does **not** load correctly, and that is now by design; see Phase 19b, *save layout*.) `platform/src/host_flash.c` keeps the chip in memory, marks it dirty on every erase/program, and `Host_SaveFlush` (after each frame, and at exit) writes it to a temp file, fsyncs and renames — a crash mid-save looks like a power cut, which the game's two-slot scheme already survives. A file of the wrong size is never overwritten (saving is disabled with a message). Verified end to end with a scripted run: Start → Save → Yes writes the file; relaunching shows CONTINUE with the saved player, time and badges; booting alone doesn't rewrite it. `platform/tests/test_save.c` covers the file handling.

*JSON save format (prototype, branch `json-save`):* the file is now `pkmemerald.json` (`platform/src/host_save_json.c`, with a small JSON reader/writer in `host_json.c`). The game still works on the in-memory flash; only the file changes:
- On flush, the slot the game's loader would pick (`GetSaveValidStatus`, ported) is decoded into its save blocks (SaveBlock1/2/3, PokemonStorage). They are written as base64 images, with readable fields beside them: player name (game text through upstream's `charmap.txt`, `{XX}` for other bytes), gender, trainer ID, play time, money, coins, flags and vars by upstream name. `info` (location, party) is read-only.
- On load, the blocks are rebuilt, the readable fields are applied over them when they differ, and one slot is packed with the game's checksums.
- The Hall of Fame, Trainer Hill and recorded-battle sectors are kept as raw sector images. When the slots aren't in a state the loader takes cleanly (a save interrupted mid-write, or damage), the file holds the raw chip instead, so the game sees exactly what it wrote.
- `layout` records the block sizes, and a save from a build with a different layout is refused rather than misread.
- An existing `pkmemerald.sav` is converted on first start and kept. `--convert-save IN OUT` converts either way.

Tests:
- `json-save-format` covers every played checkpoint: it loads identically through the game's loader and converts stably. It also covers edits, refused edits, the raw fallback and the migration.
- `test_json` covers the JSON code, and `test_save` covers the file handling.
- Verified in the game: a hand-edited name shows on CONTINUE, and saving over a JSON-loaded save writes counter+1.

*Step 1 done (branch `json-save-fields`, format version 2): every save-block field is named, and there is no base64 in the save blocks.*
- **The field table:** `platform/src/host_save_layout.inc` lists every field of SaveBlock1/2/3 and PokemonStorage, with its offset, size, kind, bitfield position, array sizes and nested type: 64 types, 570 layout checks.
  - It is generated by `platform/tools/save_layout.py` (pyelftools) from the debug info of the `save-layout-probe` object, and committed, so builds (Windows too) need neither pyelftools nor debug info.
  - It contains `_Static_assert`s on every named type's size and member offsets, so an upstream struct change fails the build until `cmake --build build --target save-layout` regenerates it. The `save-layout-up-to-date` test (when pyelftools is found) also catches changes the asserts can't, such as a new field placed in old padding.
- **The walker** (`host_save_json.c`) writes each block as an object of named fields: numbers, nested objects, arrays (numbers on one line). A field that is 0 is left out, and so are trailing zero array entries. Reading starts from zeros, so the file is compact (67 KB for a checkpoint).
  - Typos, out-of-range values, too-long arrays and fields that `game.player`/`flags`/`vars` own are refused with their path, e.g. `blocks.save_block_2.optionsTextSpeed must be a whole number from 0 to 7`.
  - The block sizes in `layout` are informational now: loading goes by name.
  - Version 1 files (base64 blocks) still load.
- **Still raw for now:**
  - Unions are lists of their bytes: TV shows, the old man, the Lilycove lady, and Pokémon data (`secure`).
  - Values XORed with the encryption key are raw numbers (bag quantities, game stats, berry powder).
  - The special sectors (Hall of Fame, Trainer Hill, recorded battle) and the raw-chip fallback stay base64.
- **Tests:** on every checkpoint, the save blocks rebuilt from JSON match the original sectors byte for byte (the game zeroes padding). Block edits load, bad block edits are refused, a changed `layout` still loads, and a version 1 file loads the same. Checked in the game: an edited name and an edited `optionsTextSpeed` survive an in-game save.

*Next:*
- Decode Pokémon: party, boxes, Day Care and secret bases (encrypted substructs plus checksum).
- Decode the unions by their type byte.
- Decode values XORed with the encryption key (`ApplyNewEncryptionKeyToAllEncryptedData` lists them).
- Turn game-text fields into strings.
- Names for species, items, moves and maps.
- Walk the special sectors.

Also in this phase:
- `pkmemerald --fast`: no waiting between frames, for scripted test runs (~4x faster at -O0).
- `platform/patches/text.c.patch`: the glyph blitter shifted 32-bit values by 32, which is 0 on ARM but a no-op on x86 (the count wraps), stamping a stray copy of a glyph's edge after some words ("OPTION|"). `-DPKM_SANITIZE_SHIFT=ON` (separate build dir) instruments the game with `-fsanitize=shift-exponent` to find more of these; the intro/title and the quick-start → Littleroot → save routes now run clean.
- Known limitation: mid-frame interrupts (Phase 11's timer) depend on real time, and each V-blank advances the RNG, so scripted runs aren't frame-exact (e.g. quick-start's random gender varies). The GBA is deterministic because it counts cycles.

---

## Phase 9 — Graphics data pipeline

**Goal**: `.4bpp`/`.gbapal`/`.lz`/`.rl`/`.smol` assets are converted to something the renderer can consume. The conversion is **offline** (build time) so the runtime never has to decode GBA formats.

**Edits**
- New `tools/gba_to_png.py` (Python + Pillow):
  - Reads `reference/graphics/**/*.4bpp` + sibling `.gbapal`, writes `assets/gfx/<name>.png` and `assets/gfx/<name>.json` (palette index list).
  - For LZ-compressed assets (most tilesets/sprites), call `reference/tools/preproc/preproc` to decompress first, or implement LZ10/LZ11 (Pokémon Emerald uses both).
- New `tools/build_assets.py`: walks `reference/data/` and `reference/graphics/`, emits one big `assets/packed.bin` + `assets/manifest.json` mapping logical IDs (e.g. `MON_PIKACHU`) to offsets.
- `CMakeLists.txt`: add a custom target `assets` that depends on the manifest, and re-runs the Python tools on `reference/` change.
- The *runtime* side (this phase and Phase 10) just `mmap`s `packed.bin` and reads by offset/length.

**Done when** `make assets` produces a valid PNG of, say, Pikachu's front sprite at the correct palette.

**Status: not needed.** Upstream's graphics pipeline (gbagfx + preproc `INCBIN`/`INCGFX`) already compiles every asset into the game in the GBA's own formats, and the game decompresses and uploads them to (emulated) VRAM itself, so the renderer reads GBA tile data directly. A separate asset pack would only matter for replacing assets.

---

## Phase 10 — Background rendering (BG layers 0–3)

**Goal**: The game's BG layer system (`src/bg.c`, `src/screen.c`) drives what the user sees.

**Edits**
- New `platform/src/render_bg.c`:
  - Maintains a 256×4 palette of `SDL_Color`.
  - Per BG layer: a software buffer of 512×512 (or 1024×1024 for affine) at 8bpp indexed; built each frame from the game's `gBgTilemapBuffer`-style state (we mirror GBA BG state into host-side structs in `src/bg.c` — or just keep GBA BG state in its own struct, indexed by `BG_*` regs).
  - `Platform_FrameEnd` blits each layer (priority-ordered, with `BLDALPHA`/`BLDCNT` blended via `SDL_SetTextureBlendMode`) to the screen.
- Stub `REG_BG0CNT`/`REG_BG1CNT`/etc. so writes to them are captured into a host-side `struct HostBgLayer`.
- Implement `RequestDma3Copy` (used heavily for tile/tilemap uploads): in the host build, just `memcpy` to the layer's CPU-side buffer.

**Done when** booting into the title screen shows the copyright screen text/gradient, and the lit Pokéball intro plays (just visuals; no music yet).

**Status (done).** `platform/src/host_render.c` is a scanline renderer following the GBA PPU rather than per-layer SDL textures: each line is drawn from the registers as they stand at that line, then that line's H-blank runs (H-blank DMA via `Host_DmaHBlank`, the H-blank interrupt, `REG_VCOUNT`), in the hardware's order (V-blank interrupt → H-blanks of lines 160–227 → lines 0–159). Covers video modes 0–5 (text, affine with per-line reference points, bitmap), priorities, mosaic, windows 0/1/outside, and alpha/brighten/darken effects. Sprites and the OBJ window are Phase 11. The copyright screen, the expansion splash, all three intro scenes' backgrounds and the title screen (affine logo, blended clouds, Rayquaza) render; screenshots via `pkmemerald -f N -o frame.ppm`.

Bugs found on the way:
- The Phase 2 `BgAffineSet`/`ObjAffineSet` had wrong scaling and swapped signs; now integer BIOS math (top 8 angle bits, 256-entry sine table), covered by `platform/tests/test_bios_affine.c`.
- `VBlankIntrWait` slept 16 ms; it now finishes the frame (interrupts, display, input, timing) for code that waits for V-blank mid-frame (crash screen, debug tools). The `main.c` patch exposes `HostMain_RaiseVBlankInterrupts` for it.
- The silent `host_m4a.c` reported all music as already finished, so the title screen returned to the intro immediately. BGM now counts as playing until stopped/faded, as the real engine's status words would show; sound effects still finish at once.

---

## Phase 11 — Sprite rendering (OBJ)

**Goal**: Up to 128 sprites are drawn per frame, with the game's affine/priority rules.

**Edits**
- New `platform/src/render_obj.c`:
  - One `SDL_Texture` per loaded sprite sheet (built at boot from `assets/manifest.json`).
  - Per OBJ: read from `gSprites[]` (mirror kept in sync), look up sheet, `SDL_RenderCopyEx` with affine rotation.
- Implement `Dma3CopyLarge`/`Dma3Fill32`/`Dma3Fill16` as plain `memcpy`/`memset`.
- Implement `OamLoad`/OAM DMA as: just refresh the host `gSprites[]` mirror.

**Done when** walking around the overworld shows the player sprite and the overworld NPCs.

**Status (done).** Sprites are part of the scanline renderer (`platform/src/host_render.c`), not SDL textures: all 128 OAM entries, every shape/size, 4/8 bpp, 1D/2D tile mapping, flips, 8-bit Y / 9-bit X wraparound, mosaic, affine (incl. double-size), sprite-vs-sprite order (lower OAM index wins ties), sprite-vs-BG priority, semi-transparent sprites and the OBJ window. With quick-start (Select on the title), the truck ride and Littleroot Town show the player, Mom and the truck, with text boxes.

Getting into the overworld surfaced GBA assumptions beyond rendering:
- **Interrupts during busy-waits.** The map loader spins until the V-blank handler's DMA3 manager has copied the tilesets; on hardware V-blank interrupts preempt the loop. `platform/src/irq_timer_posix.c` (SIGALRM, armed only while game code runs) now raises the V-count/V-blank interrupts every GBA frame time if a frame overruns, like a lag frame. A Windows build needs its own timer (Phase 17).
- **The ROM mirror.** Script commands/specials that "request effects" are tagged by adding `ROM_SIZE` to their pointer and called anyway — on the GBA the cartridge is mirrored at `0x0A000000`. `platform/patches/script.c.patch` and `scrcmd.c.patch` strip the tag at the call sites (`CALLABLE()`); `host_gba_misc.c` checks at startup that code lies in `0x08000000–0x0A000000` so the tag test keeps working.
- **RAM by address.** `bg.c`'s `IsTileMapOutsideWram` treated every buffer above `IWRAM_END` as ROM, which would have stopped all text windows from drawing; `bg.c.patch` asks `Host_IsGbaRamPointer` instead.
- **Fade timing.** `host_m4a.c` fades now take 16 × `speed` frames like the real engine: quick-start fades the music and checks "has it ended?" in the same frame, and the instant fade sent it back to the copyright screen.
- `pkmemerald -i SCRIPT` presses buttons at given frames (e.g. `600:a,700:b,760:b,840:select`), for reproducible test runs. CMake now re-configures when a patch file is added.

---

## Phase 12 — Text rendering

**Goal**: The font system (`src/text.c`, `src/window.c`) renders correctly on PC. This is a small phase because Pokémon Emerald's text is mostly drawn as tile bitmaps — Phase 10's renderer handles it once the font tiles are part of `assets/`.

**Edits**
- Bake the JP/EN font tiles (`reference/graphics/fonts/`) into `assets/font_en.png` and `assets/font_jp.png` during Phase 9.
- `src/window.c`'s `PutWindowTilemap`/`CopyWindowToVram` already produce tile data — our BG renderer just consumes it.
- Add a `DecompressDataWithHeader` (or whatever the LZ wrapper is) on the host side — this is a host reimplementation of GBA LZ10/LZ11, not a port of the ARM code.

**Done when** the overworld HUD ("PLAYER • 0123 / 0234") renders with the right font, kerning, and palette.

**Status (done; no separate work was needed).** The game draws its own text into window tiles, which the Phase 10 renderer displays: dialogue, menus, the summary screen, the Pokédex and battle text render correctly. The only fix was in the glyph blitter: `text.c.patch` replaces shifts by ≥ 32 (0 on ARM, wrapped on x86), which had drawn stray bars next to glyphs.

---

## Phase 13 — Audio (M4A → SDL_mixer)

**Goal**: Music and SFX play. This is one of the two "big" phases.

**Edits**
- `sound/` is excluded from the build. We don't port the M4A engine; we **pre-bake** music to Ogg Vorbis (.ogg) at build time and play it back.
- New `tools/song_to_ogg.py`:
  - Input: a song in `reference/sound/songs/midi/*.mid` (these are the original midis before they were packed into the ROM). The submodule has them in `reference/sound/songs/midi/`.
  - For each, run `timidity` or `fluidsynth` (whichever is on the host) with a soundfont, output `.ogg`.
  - For voice groups, we use the SF2/OPN-style approach: convert `voice_groups.inc` to a `vgs.json` once, then `fluidsynth -F out.wav` and encode to `.ogg`.
- New `platform/src/audio.c`:
  - `Platform_Init` opens `SDL_AudioSpec` at 44.1 kHz stereo.
  - `PlaySE(id)`: look up `assets/se/<id>.ogg` in the manifest, play once on a free channel.
  - `PlayBGM(id)`: cross-fade the current BGM track to a new one.
  - Map the game's `m4a_*.h` API to the host layer: `m4a_play_song` → `PlayBGM`, `m4a_song_num_play` → `PlaySE`, etc.
- The game's `src/m4a.c` and `sound/MPlay*.s` are excluded from the build entirely. Call sites in `src/*.c` call into the host layer through thin shim headers in `platform/include/`.

**Done when** booting into the title plays music, and the overworld BGM plays without crackle at 60 fps.

**Status (done).** Not the outline above: the game's own sound engine runs, so music, sound effects and cries play exactly as the game drives them (fades, ducking under cries, the Pokédex cry waveform, which reads the mix buffer). `reference/src/m4a.c` and `m4a_tables.c` are built as-is plus `platform/patches/m4a.c.patch` (NULL players are ignored, no BIOS call, no wait for `VCOUNT`). `platform/src/m4a_engine.c` is a C translation of `m4a_1.s` (sequencer, track commands, note allocation, and the software mixer with the expansion's compressed/reversed cry samples), keeping the original arithmetic. `platform/src/host_audio.c` is the sound hardware: the four PSG channels emulated from the registers `CgbSound` writes (triggers, envelopes, sweep, length, wave RAM, noise LFSR, frame sequencer), mixed with Direct Sound the way `SOUNDCNT_*`/`SOUNDBIAS` mix them, at 48 kHz into a ring buffer that `audio_sdl2.c` plays; the output rate is nudged by up to ±0.5% to keep ~50 ms buffered. Tests: `test_m4a_engine` (music, a sound effect, compressed and reversed cries, fade-out) and `test_m4a_null_player`.
- The interrupt timer's SIGALRM can be delivered to any thread, e.g. SDL's audio thread; it is now forwarded to the game thread, so game interrupts never run beside the game.
- Checked against mGBA running the real ROM (built from `reference/` with upstream's toolchain): the game's mix buffer matches mGBA's at the same frame, and the output tracks mGBA's at a constant ratio (64/48, the two output scales) through the intro. That comparison found the host never set `SOUNDBIAS` to its BIOS power-on value (0x200): with bias 0 the DAC clipped the negative half of every Direct Sound sample, so instruments came out quiet and distorted under the PSG ("crunchy", "too much bass"). `RegisterRamReset`'s serial-register reset also cleared the sound control registers (wrong offset); both fixed in `host_hal.c`.
- Output style: by default the exact DAC output (held samples, 8-bit steps), as emulators play it; `--smooth-sound` interpolates Direct Sound and low-passes at 10 kHz (standing in for the analog stage after the GBA's DAC), ~12 dB less hash above the 6.7 kHz mixing Nyquist. The device reports underruns/dropped audio on exit (0 in a 15 s PulseAudio run).
- `--fast` runs are silent (they run far faster than real time); `-a FILE` records the sound of any run to a WAV file, which is how the output was checked without listening.

---

## Phase 14 — Overworld

**Goal**: You can walk around a map. (Engine is mostly already in `src/overworld.c`; this phase just exercises it.)

**Edits**
- No new platform code expected — this is verification. Run the game, walk around Littleroot Town, enter a building, talk to an NPC, exit.
- If the camera is misbehaving, the issue is in Phase 10's BG math.
- If the player sprite snaps, it's Phase 11.

**Done when** a 5-minute walkthrough plays end-to-end with no visual glitches.

**Status (in progress, by playtesting).** Littleroot, the houses and the lab, Route 101, NPCs, the Pokémon Center (PC storage) and the Pokédex work without visual glitches. Fixes found this way: script pointers tagged with the ROM mirror (`script.c`/`scrcmd.c` patches), the map loader waiting on interrupts (mid-frame interrupt timer), and the crashes listed in `known_crashes.md`. Continues as the game is played further.

---

## Phase 15 — Battle system

**Goal**: Battles work end-to-end. The battle engine is platform-agnostic; the work is in the *presentation* layer.

**Edits**
- Battle background rendering: extend Phase 10 with battle BG support (the existing game already drives it; we just need to support the `bg2_affine` mode).
- Battle animations: `src/battle_anim.c` drives sprite cells, palettes, and BG effects. Most of this already works through the Phase 11 sprite code; we add specific support for battle-specific features (alpha blending, mosaic) in `platform/src/render_blend.c`.
- Move animations are sprite swaps; mostly already work.

**Done when** a wild battle starts, the intro anim plays, you select moves, and the battle resolves with HP bars, exp gain, and the victory music.

**Status (in progress, by playtesting).** Wild battles work end to end with no battle-specific renderer work: transitions, intro animations, move animations, HP/exp bars, music, catching (Poké Ball throw), nicknaming and learning moves. Fixed on the way: `battle_transition.c`, `pokemon_animation.c`, `item_use.c` and the naming-screen and sprite patches (see `known_crashes.md`). Trainer battles and later move animations are still to be seen.

---

## Phase 16 — Multiplayer (RFU → UDP) — *optional*

**Goal**: Trade/battle over LAN, the way `librfu_*` provides on GBA.

**Edits**
- Exclude `reference/src/librfu_*.c` and `AgbRfu_LinkManager.c` from the build.
- New `platform/src/net_rfu.c`:
  - Implements the same `Rfu*` API surface as `AgbRfu_LinkManager` (`Rfu_Init`, `Rfu_SetPlayerName`, `Rfu_StartSearchParent`, etc.).
  - Backed by UDP multicast on `239.255.0.1:4999` (chosen by mGBA-quark for the same role).
  - Discovery packets: reimplement the GBA RFU discovery handshake (about 4 message types) on top of UDP datagrams.
- Wire `src/link.c`/`src/link_rfu.c` to call into `net_rfu.c` instead.

**Done when** two PC instances on the same LAN can see each other in the Union Room. (Defer to a follow-up if not on the critical path — single-player is the main goal.)

**Status: not started (optional).** The link and wireless code currently talks to stubs (`host_rfu.c`), so link features report no partner.

---

## Phase 17 — Polish & platform builds

**Goal**: Anyone with the source can build and play the game on Linux and Windows (and macOS after Phase 18), from one documented set of commands per platform.

**Source only — no binary downloads.** The built game contains Nintendo's assets (graphics, music, text from `reference/`), so the project publishes source and build instructions only: no GitHub Releases, no download links, and CI uploads no build artifacts (artifacts on a public repo can be downloaded by any logged-in user). Packaging targets exist so a user can make a package of their own build for their own machines.

**Edits**
- **Release builds**: fix `RelWithDebInfo`/`Release` (`NDEBUG` changes the `AGBPrintInit` macro that `host_hal.c` defines). Default to `RelWithDebInfo` for players.
- **Linux** (x86, 32-bit for now): `cmake --install` lays out the binary and a data/save location; optional CPack target for a local `.tar.gz`/AppImage that bundles the 32-bit SDL2.
- **Windows** (32-bit for now): build with MinGW-w64 i686, either natively under MSYS2 or cross-compiled from Linux with a CMake toolchain file (`cmake/mingw-i686.cmake`). The upstream host tools (`gbagfx`, `preproc`, `mid2agb`, …) and the asset preparation must still be built and run for the *build* machine, so the prepare step needs its own host compiler when cross-compiling. Needs:
  - an interrupt timer to replace `irq_timer_posix.c` (SIGALRM): `platform/src/irq_timer_win32.c`;
  - a crash handler to replace `crash_handler_linux.c`: `platform/src/crash_handler_win32.c` (`SetUnhandledExceptionFilter`, same report format);
  - save-file writes that work on Windows (atomic replace with `MoveFileEx`/`ReplaceFile` instead of `rename`, no `fsync`);
  - an install/zip target that puts `SDL2.dll` next to the `.exe`.
- **macOS**: after Phase 18 (macOS runs only 64-bit programs). Needs its own interrupt timer and crash handler (the POSIX timer may carry over), and an app-bundle install target; ad-hoc signing is enough to run a build on your own Mac.
- **Options**: a launch-time options screen or config file (save dir, fullscreen, window size, language), on top of the existing command-line flags.
- **CI** (GitHub Actions): build and run `ctest` for Linux and Windows (and macOS after Phase 18), caching `reference/`. Build and test only; nothing is uploaded.
- Re-enable `-Wall -Wextra -Werror` once warnings are addressed.
- **Documentation**: `README.md` build instructions per platform, including that no binaries are provided and why; a `CONTRIBUTING_PC.md` explaining what's safe to merge from upstream.

**Done when** following `README.md` on a clean Linux and a clean Windows machine builds the game, which runs, saves, quits, relaunches and continues with all progress intact; and CI builds and tests both platforms.

**Status: done.** Already in place: integer-scaled window (`-x SCALE`, default 3×), saves that survive crashes and restarts, crash reports (Linux), `README.md` build instructions (Linux).
- *Done (Linux):* `RelWithDebInfo` is the default build type; CMake's `-DNDEBUG` is stripped so build types only change optimisation (the game's own `RELEASE` switch still picks its debug/release configuration). 32-bit builds use SSE2 maths (`-msse2 -mfpmath=sse`): with x87, the sound mix differed by ±1 LSB between Debug and Release; now both produce byte-identical audio and identical frames on scripted runs. The optimised build runs ~3× faster than `-O0`. `cmake --install` installs the binary and a `.desktop` launcher; `--target package` makes a local `.tar.gz`. Saves and crash reports default to the per-user data directory (`SDL_GetPrefPath`: `~/.local/share/pkmemerald/`, `%APPDATA%\pkmemerald\` on Windows), except that an existing `./saves/pkmemerald.sav` keeps being used.
- *Done (Windows):* cross-compiled from Linux with MinGW-w64 i686 (`cmake/mingw-i686.cmake`, SDL2 from `tools/fetch_sdl2_mingw.sh`); `--target package` makes a `.zip` with `SDL2.dll`. All 10 tests pass under Wine (`test_crash_handler` is POSIX-only), `data-abi-matches-gba` included; a scripted continue-walk-Pokédex run gives the same final frame as Linux; paced play runs at 60 fps with WASAPI sound. What it took:
  - *Struct layout:* the Windows ABI aligns 64-bit integers to 8 and packs bitfields the Microsoft way. `u64`/`s64` are typedef'd 4-byte aligned (`gba/types.h`), and game code builds with `-mno-ms-bitfields` (not the SDL/Win32 files). `verify_data_abi.py` now reads layouts from DWARF (`objdump --dwarf=info`, no gdb) and handles COFF objects, so the check covers Windows.
  - *Assembly data:* `host_assemble.sh` drops ELF-only `.type`/`.size`, converts `%progbits` section flags, and adds the i386 Windows `_` prefix to every global symbol of the assembled objects (`objcopy --redefine-syms`). No `--noexecstack`.
  - *C data:* `COMPOUND_STRING` sections all go to `.rdata$compound_string` (each distinct section became a 4 KiB-aligned PE section: 8994 of them, which Windows/Wine refuse to load). The weak definitions in `random.c`/`test_runner_stub.c` are made strong (patches): MinGW's linker doesn't resolve weak aliases from a static library, and the test runner that overrides them isn't built.
  - *Memory map:* fixed image base 0x08000000 without ASLR keeps code in the GBA ROM window; EWRAM sections are bracketed by `.data$ewram_*_a/_z` markers (PE has no `__start_`/`__stop_`); RAM bounds from MinGW's `__data_start__`/`__bss_end__`.
  - *Platform:* `irq_timer_win32.c` (a timer thread suspends the game thread and, only while it runs the game's own code, redirects it into an assembly trampoline that saves all registers and SSE state); `crash_handler_win32.c` (unhandled-exception filter + SIGABRT, COFF symbol table, frame-pointer stack walk: Windows builds use `-fno-omit-frame-pointer`); `host_fs_win32.c` (atomic save via `MoveFileEx`); `alloca.h` shim; `SDL_MAIN_HANDLED`.
- *Done (CI):* `.github/workflows/ci.yml` builds and tests Linux and Windows (cross-compiled, tests under Wine) on every push to `main` and every pull request. Build and test only: no artifact uploads, no releases, and no caches of build output (a fork's pull request can restore the repository's caches), so nothing built is ever downloadable. The one cache is what upstream's public sources generate in `reference/` (host tools, converted graphics and sound; `tools/ci_reference_cache.sh`, which refuses to pack ROMs, ELFs, object files or executables), keyed on the upstream commit: it cuts asset preparation from ~2.5 min to seconds; the game itself is compiled from scratch every run. Documentation-only changes (`**.md`) skip CI. Permissions are read-only.
- *Done (settings):* `pkmemerald.ini` in the save directory (`host_config.c`), written with comments and defaults on first start and never rewritten: `[video]` scale, fullscreen; `[sound]` sound, smooth_sound; `[keyboard]` keys per GBA button (SDL key names; a list with an unknown name falls back to that button's default, an empty list means no key). Defaults < file < command-line flags (`--config FILE`, `-x`, `--fullscreen`/`--windowed`, `--mute`, `--smooth-sound`). Mistakes are reported and keep the default. New: borderless fullscreen, F11 / Alt+Enter toggle (not passed to the game). Instead of a launch-time options screen: the file plus the in-game Options menu. Tests: `test_config` (parser, mistakes, file creation), `test_input` (custom layouts, Alt+Enter).
- *Done (Windows polish):* installed and packaged copies are stripped of debug info but keep the symbol table crash reports use (Windows `.exe` 103 → 39 MB, zip 20 MB; Linux 69 → 38 MB). The `.exe` is a windowed program: no console window; messages go to `pkmemerald-log.txt` in the save directory unless output is redirected, and `--console` attaches to the starting console or opens one (`console_win32.c`). CI uses `actions/checkout@v5`.
- *Real Windows (2026-10-06):* boots and walks into a new map (so the interrupt timer works there). Not yet checked on real Windows: sound, `--console`/log file, fullscreen.
- *Done (warnings):* `platform/` code (library sources, tests, entry point) builds with the warnings the project disables for upstream turned back on (unused variables/functions, implicit declarations, pointer/integer conversions), and with `-DPKM_WERROR=ON` (CI) as errors; off by default so a newer compiler can't stop a player's build. Upstream headers reach platform code via `-idirafter` + `-fno-canonical-system-headers`, so they count as system headers (their warnings don't count) while still resolving through the host include tree; checked: all 31 platform files include exactly the same headers as before. It found a real bug: the Windows EWRAM reset `memset` from zero-size marker arrays (undefined: the compiler may drop it), now done through pointers whose origin is hidden.
- *Done (docs):* `CONTRIBUTING_PC.md`: ground rules (source only, `reference/` untouched, PRs, crash log), where code goes, writing patches, updating upstream step by step, CI. `tools/check_patches.sh` dry-runs every patch against `reference/` and reports failing or inexact ones (first step after an upstream update).
- Note: `-d DATA_DIR` is never read (the game data is compiled into the executable). The build is 32-bit only (the game assumes 32-bit pointers), so Linux and Windows builds need 32-bit SDL2; making it 64-bit is Phase 18.

---

## Phase 18 — 64-bit build — *needed for macOS*

**Goal**: Build and run as a native 64-bit binary (`-DPKM_HOST_32BIT=OFF`) with no behaviour change. macOS requires it: it hasn't run 32-bit programs since Catalina, and Apple Silicon is arm64 only. On Linux and Windows the 32-bit build runs fine, so this phase is not needed to ship there.

**Survey (2026-10-06, trial 64-bit compile of `pkmemerald-core`):**
- **Assembly data hard-codes 4-byte pointers** (`.4byte label`). The current build's asm objects hold ~61,000 of them: event scripts 18.1k, battle anim scripts 15.4k, songs 11.0k (530 files), voicegroups (`sound_data`) 5.3k, map headers/connections (`maps`) 4.4k, map events 4.1k, battle/contest AI/field effect scripts 2.8k. Two kinds:
  - *Tables C reads as structs or pointer arrays* (map headers, object/warp/coord/bg events, connections, `ToneData` voicegroups, song headers, anim and battle-script pointer tables). On 64-bit these silently mismatch the C layout — no compiler warning.
  - *Pointers inside bytecode* (event, battle, anim, contest AI, field effect, movement scripts; song `GOTO`/`PATT`), read with `ScriptReadWord`, `T1_READ_PTR`, etc. and cast to pointers.
- **411 pointer↔integer cast warnings in ~60 files**, normally hidden by `-Wno-pointer-to-int-cast`/`-Wno-int-to-pointer-cast` in `CMakeLists.txt`. Most are the bytecode readers (`contest_ai.c` 100, `scrcmd.c` 44); the rest store pointers in 32-bit task/sprite data (`SetWordTaskArg` and friends, `battle_factory_screen.c` 29). Platform code is clean apart from an implicit declaration of `MidiKeyToFreq` in `m4a_engine.c`.
- **Two compile errors** (size asserts): `struct ListMenu` holds pointers and outgrows task data (`list_menu.c`); `struct RecordedBattleSave` grows because `u64` is 8-byte aligned on x86-64 but 4-byte on the GBA (`recorded_battle.c`). The latter also changes save-struct padding, which would break `.sav` compatibility.

**Checks first** (they can land in the 32-bit build and must pass there before anything changes, so that the 64-bit work fails at build or `ctest` time rather than in play):
- **asm layout asserts** (the Linux kernel's `asm-offsets.c` trick): a host C file compiled before the asm emits `sizeof`/`offsetof` of every struct the asm fills (`MapHeader`, `MapEvents`, object/warp/coord/bg event templates, connections, `ToneData`, `SongHeader`, …) into a generated `.inc`. Host versions of the `asm/macros/*.inc` macros end each entry with `.if (. - start) != SIZEOF_<Struct>` … `.error`, so a 4-vs-8-byte mismatch stops the build at the offending file and line.
- **Pointer warnings as errors**: once the cast sites are fixed, replace `-Wno-pointer-to-int-cast`/`-Wno-int-to-pointer-cast` with `-Werror=` for both (and for `int-conversion`), so new upstream code that assumes 32-bit pointers fails to compile.
- **Data-walk test** (`ctest`): walk every map header, event list, connection, voicegroup, song header and script pointer table (battle, anim, contest AI, field effect, mystery event), and check that each pointer lands inside the game's data. This covers every map without playing it.
- **Debug decode check**: the self-relative bytecode decode helper asserts (in debug builds) that the result lands inside the game's data and reports the script and offset; a reader that was not converted gives a wild pointer and crashes at once.
- **Sanitizers**: run `ctest` and a scripted boot of the 64-bit build under AddressSanitizer and UBSan (out-of-bounds and misaligned reads).
- Still invisible to all of these: pointers copied as raw bytes (`memcpy`) or held in a `u32` inside a union. Rare; playtesting covers them.

**Edits**
- `platform/include/gba/types.h`: `typedef uint64_t u64 __attribute__((aligned(4)))` (and `s64`), so save structs keep the GBA layout. Add a test that the save block sizes match the 32-bit build.
- Struct tables: host versions of the `asm/macros/*.inc` macros (map, events, voicegroups, song headers, pointer tables) that emit pointer-sized, C-aligned slots (`.8byte` + `.p2align 3`), applied through `tools/host_assemble.sh`; `reference/` stays unmodified. Check each against its C struct with a size/offset test.
- Bytecode pointers: keep them 4 bytes but self-relative (`.4byte label - .`), and patch the readers to decode `slot + (s32)value`: `script.c`, `scrcmd.c`, battle script commands, `battle_anim.c`, `contest_ai.c`, field effect scripts, movement, `mystery_event_script.c`, and the song sequencer in `m4a_engine.c`. (Widening to 8 bytes would mean finding every place that skips an operand by 4.)
- Pointers in task/sprite data: patch the `SetWordTaskArg`-style sites (or add a host handle table), and make `ListMenu` fit.
- Fix every remaining cast warning, then turn the pointer/int flags into errors (see checks above).
- CMake: 64-bit SDL2 (`libsdl2-dev`), and flip `PKM_HOST_32BIT` to default OFF once 64-bit passes playtesting.

**Shortcut (Linux/Windows only, not macOS):** linking without PIE (`-no-pie`; on Windows a low image base) keeps all static data — including `gHeap` and the emulated RAM — below 4 GB, so 4-byte bytecode pointers and the casts round-trip unchanged. Only the struct tables and the two compile errors need fixing (~1 week). It doesn't work on macOS, where 64-bit programs always load above 4 GB and arm64 requires PIE.

**Done when** the 64-bit build compiles with the layout asserts and pointer-warning errors on, passes `ctest` (including the data-walk test) clean under ASan/UBSan, a save from the 32-bit build loads and plays on it (and vice versa), and the Phase 14–15 playtest route (intro, Littleroot, Route 101, battles, catching, PC, Pokédex, music) behaves identically.

**Status: not started.** Do after Phase 17 unless macOS becomes a target sooner. Without the checks above, most failures would show up as wrong data at runtime; with them, the build and `ctest` should catch nearly all of them.

---

## Phase 19 — Automated bug finding

**Goal**: Find the port's bugs automatically, on every pull request, instead of mostly by playtesting. Playtesting stays for what tools can't judge (graphics glitches, sound, feel); crashes, memory errors and logic differences from the GBA build should be caught by CI.

Three parts, in this order (quickest win first, biggest coverage second):

### 19a — Sanitizers on scripted runs (~1 day)

Nearly every crash in [known_crashes.md](known_crashes.md) was a read through NULL or past an array that the GBA tolerates. AddressSanitizer and UndefinedBehaviorSanitizer report exactly that, where it happens, even when the game doesn't crash. Both work for the 32-bit Linux build (checked: `gcc -m32 -fsanitize=address,undefined`; Debian/Ubuntu `lib32asan8`, `lib32ubsan1`).

**Edits**
- CMake option `PKM_SANITIZE` (`address,undefined`), for the game and the tests.
- Scripted playthroughs (`-i` input scripts with `--fast`, from a fixed save) as tests: boot and intro; continue, walk across maps, a wild battle, catching, the start menu (Pokédex, party, bag, summary), the Pokémon Center PC, saving. Kept as files under `platform/tests/scripts/`.
- A CI job: Linux build with `PKM_SANITIZE`, running the tests and the scripted runs; any report fails it.

**Things to sort out**
- The game's own heap (`gHeap`, upstream's `malloc.c`) is one big array to ASan, so overruns inside it go unseen. Mark freed and unallocated blocks with ASan's poisoning interface (`ASAN_POISON_MEMORY_REGION`) in a sanitizer build (a patch to `malloc.c` or a host wrapper).
- Signal handlers: the crash handler and the SIGALRM interrupt timer must coexist with ASan's (crash handler off in sanitizer builds; `ASAN_OPTIONS` for the alternate stack).
- Upstream code that the GBA build relies on but is undefined in C (shifts ≥ 32, signed overflow; `-fwrapv` is already on): report, then decide per case between a patch and a suppression list.

**Done when** the CI sanitizer job runs the scripted playthroughs clean, and a deliberately reintroduced known crash (e.g. reverting part of `pokedex.c.patch`) is reported with its file and line.

**Status (done).** How it ended up:
- `-DPKM_SANITIZE=address,undefined` (Linux): the game, the tests and the crash handler (which steps aside, so the sanitizer reports crashes). `shift-base` isn't checked (GCC defines signed shifts into the sign bit, and upstream's DMA macros do it on every call); shifts by the width or more still are.
- Instead of hand-written input scripts: `--monkey SEED[@FRAME]`, deterministic pseudo-random input (walking, A/B, sometimes menus; never the soft-reset chord). Monkey tests run in every build (they catch plain crashes too): two from boot (intro, title menu, Birch's speech, naming) and two after a quick start (Select on the title: a new game in the truck, then the house, Mom, the start menu), 20,000 frames each. A CI job runs everything with the sanitizers.
- Checked: reverting `trainer_card.c.patch` makes `monkey-quickstart-2` fail with `trainer_card.c:1584`.
- Found and fixed (`known_crashes.md`): a crash on closing the trainer card; reads past arrays in door drawing, the quick-start label palette, the region map palette, compressed tileset copies; a pointer into the NULL `gBattleStruct` outside battles. The smol tANS decoders' one-word read-ahead is exempted (`HOST_NO_ASAN`).
- Heap checking (`malloc.c.patch`): the game heap's unused memory is marked off-limits, on by default in sanitizer builds (`PKM_ASAN_HEAP=0` turns it off), so CI covers it. It found upstream heap misuse that behaves the same on the GBA (`known_crashes.md`), all fixed: DMA3 copies queued from blocks freed before the V-blank (freeing a block now snapshots pending copies from it; `test_dma_free`), item icon sprites pointing at a freed template, a glyph read-modify-writing past a window's buffer, and a sprite sheet loaded at more than its decompressed size. `MoveSaveBlocks_ResetHeap` uses the heap as scratch on purpose (`load_save.c.patch` allows it). To look at with 19b/19c: `data.c` points battler sprite images at a fixed heap area (`gHeap + 0x8000`) outside the allocator.
- Caveat: the interrupt timer (SIGALRM) can preempt ASan's own runtime code; harmless so far, but a run that fails inside the sanitizer runtime should be read with that in mind.

### 19a-2 — Played checkpoint saves and a fixed clock (~1 day plus playthrough)

Use checkpoints saved through ordinary gameplay rather than constructing game state or setting story flags in code. Inspection checks integrity and exposes unexpected state; it cannot prove gameplay provenance or every story invariant.

- **Fixed clock**: `PKM_FIXED_TIME="YYYY-MM-DD HH:MM:SS"` starts the test RTC at a UTC time and advances it with game frames. Record the value used when collecting saves as well as when replaying them. Scripted input supports `-i @FILE` with comments.
- **Collect natural checkpoints**: in the truck immediately after Birch’s introduction; after setting the bedroom clock; after meeting the rival before Route 101; after rescuing Birch and receiving the starter; after receiving the Pokédex; then Petalburg Woods/Route 104 with a small party, and Rustboro after the first badge. Use the normal SAVE menu when control is free. Never edit flags or inject a save mid-script.
- **Read-only inspection**: `--inspect-save FILE` loads with the game's save code and reports location, party, bag, money, set flags (including badges), and nonzero variables with upstream names. The Python wrapper checks file hashes and compares successive checkpoints. Review differences against the playthrough before accepting a fixture. See `platform/tests/saves/README.md`.
- **Fixtures and regression runs**: Seven user-played checkpoints are stored as full JSON archives in `platform/tests/fixtures/checkpoints/`, with a manifest (upstream commit, SHA-256, expected state; collection build/clock unknown). CTest reconstructs fresh copies and verifies game loading and decoded state. Tests copy approved fixtures into temporary save directories before running; never overwrite source checkpoints or a personal save. Use a fixed clock for scripted/monkey runs from at least two checkpoints, including battles. Do not upload save fixtures or completed game builds as CI artifacts.

**Done when** the read-only inspector is verified, user-played checkpoints are reviewed, two identical runs from an approved checkpoint produce identical frames, and CI runs monkey tests from at least two checkpoints including a battle-capable one. Collection, JSON reconstruction, load checks and state comparisons are implemented. Fixed-clock repeatability and checkpoint monkey runs remain pending.

### 19b — Upstream's test suite on the host (~2–4 days)

`reference/test/` holds ~6,000 tests in 967 files: ~4,540 battle tests (single, double, multi, wild), ~600 battle AI tests, ~920 others (Pokémon data, bag, party menu, day care, overworld movement, compression, ...). They pass on upstream's GBA build, so any failure on the host is a port bug.

Upstream runs them in mGBA (`make check`: a test ROM, `mgba-rom-test` and the parallel `mgba-rom-test-hydra`). The tests and the battle test framework (`test/test_runner_battle.c`, `include/test/`) are plain C over the game and can be reused as they are; the runner (`test/test_runner.c`, ~1,200 lines) is GBA-specific and needs a host version:

| GBA runner | Host runner |
|---|---|
| prints through mGBA's debug registers (`0x4FFF600`) | stdout |
| soft-resets between tests; state in a `.persistent` section that survives the reset | one fresh game state per test: a child process per test (fork on Linux; on Windows, re-run the executable with the test's index) |
| timeouts from GBA timer 2 | the interrupt-timer approach (`irq_timer_*`) or the parent process killing the child |
| ARM assembly to unwind a failed test | `setjmp`/`longjmp` |
| tests spread over several emulator processes (Hydra) | split by index across processes, and across CI cores |
| test list from `__start_tests`/`__stop_tests` | the same on Linux; `.data$`-style markers on Windows, as for EWRAM |

**Things to sort out**
- A separate test build (`TESTING=1`) of the game sources, beside the normal one.
- The test framework overrides `RandomUniform` and friends; the PC build made them non-weak (`random.c.patch`, for MinGW). The test build needs them overridable again (weak on Linux; on Windows, e.g. the test runner's definitions linked ahead of `random.c`'s).
- Tests that depend on GBA timing or hardware: expect some; mark them known-failing with a reason rather than letting them hide new failures.
- Run time: thousands of battles; split across CI cores and keep an eye on the CI budget (maybe the full suite on `main` and a subset on pull requests).

**Done when** the suite builds and runs on Linux and Windows (under Wine), CI runs it, and every failure is either fixed or listed as a known GBA/host difference with a reason.

**Steps** (one pull request each; port bugs found along the way get their own fixes and `known_crashes.md` entries; re-estimate after 19b-1, which shows how much of the setup depends on the GBA):

| Step | What | Done when |
|---|---|---|
| 19b-1 Runner skeleton | A `TESTING=1` build of the game sources beside the normal one; a host test runner that finds the registered tests, runs each in its own process, reports pass/fail, handles timeouts. Proved on one small test file (e.g. `test/compression`). Linux only. | That file passes on the host; a deliberately broken test fails with the right message. |
| 19b-2 Non-battle tests | All ~920 plain `TEST()`s (Pokémon data, bag, party menu, day care, overworld movement, ...). Each failure is investigated: a port bug is fixed; a real GBA-only difference goes on a known-failures list with the reason. | All pass or are listed with a reason; CI runs them. |
| 19b-3 Battle framework | `test_runner_battle.c` on the host, including `RandomUniform` and friends overridable again in the test build. Proved on a few hundred single-battle tests. | Those pass or are listed with a reason. |
| 19b-4 All battle + AI tests | The remaining ~5,000 (double, multi, wild, AI). First split CI (see *CI time* below): a separate upstream-tests job over several machines, and a path filter; if still too slow for every PR, a subset on PRs and all of it on `main`. | Same rule; CI runs them, and the slowest job stays well under its 60-minute timeout. |
| 19b-5 Windows | The suite on the MinGW build under Wine: test registration via `.data$`-style markers, per-test processes by re-running the executable. Could move after 19c: the game logic is the same code on both platforms. | Runs in the Windows CI job. |

**Status: in progress.**
- *19b-1 done:* `-DPKM_UPSTREAM_TESTS=ON` (Linux) builds the game a second time with `TESTING=1` (`pkm_preprocess`/`pkm_core_library` in CMake), plus upstream's runner and tests, as `pkmemerald-tests [PATTERN]` (`platform/upstream_tests/`). Upstream's runner runs unchanged except for `test/test_runner.c.patch`: mGBA output and exit go to the host (printed and counted like Hydra), `JumpToAgbMainLoop` is a `siglongjmp` to the frame loop, the persistent state is in memory shared with a parent process that restarts a crashed child (the runner then reports CRASH and goes on, as after a GBA soft reset), timer 2 ticks every 60 frames, a stuck child is killed after 60 s. `__FILE__` is mapped to upstream's relative paths (`-fmacro-prefix-map`) so file filters work. Upstream NULL accesses the GBA tolerates, fixed: `MoveSaveBlocks_ResetHeap` copying from the unset `gSaveBlock1Ptr` at test boot (`load_save.c.patch`), `FunctionTest_SetUp` clearing the rigged-RNG list before allocating it. Passing: `test/fpmath.c`; the runner's own crash, `fatalf` and save-block tests; a deliberately failing test is reported with upstream's message (`host_runner_selftest.c`). `PKM_TESTS_NO_FORK=1` runs in one process for a debugger.
- *19b-3 done (taken before 19b-2):* the battle framework runs on the host, with `test/test_test_runner.c` and all of `test/battle/move_effect/` (345 files, ~2,000 tests) in the test build and CTest. All pass (260 are upstream's own TO_DO). What it took (`test/test_runner_battle.c.patch`):
  - The test function is called once per phase (GIVEN, WHEN, SCENE, THEN), and its locals must survive between calls (`HP_BAR(..., captureDamage: &damage)` is written while the battle runs and read in THEN). Upstream runs it on a stack of its own (ARM assembly); the host does the same with `ucontext`, on a 256 KiB stack cleared at each test.
  - The framework keeps addresses of those locals and of the results in 27/28-bit fields, which GBA RAM addresses fit. The runner's state (normally overlaid on `sBackupMapData`) and that stack are in memory mapped at 0x02000000, where the GBA's EWRAM is (`platform/host_test_ram.h`). The program itself can't move lower: tagged script pointers need its code in the ROM window.
  - Battles start with no map loaded; the environment lookup read through the NULL map layout (`fieldmap.c.patch`; the tests set the environment they need).
  - A `PLAYER(...)` missing its `;` upstream (Beat Up, Shed Tail) closes a Pokémon twice; the second close is skipped, as its writes go to BIOS memory on the GBA.
  - Port bugs found, which crash the game too (`known_crashes.md`): battle animations played with a `NULL` argument (Disguise, Z-Moves, Shell Trap, ...), Hex/Venoshock against a statused target, Dynamaxing, a ball with capture odds 0 (division by zero).
  - `RandomUniform` and friends were already weak in the test build (`random.c.patch`).
- *Sharding:* `pkmemerald-tests -j N` (default: the number of CPUs, at most 32) splits the tests as Hydra does, through upstream's own `gTestRunnerN`/`gTestRunnerI` cost balancing: N shards, each with its own child processes and restarts; one summary. All move-effect tests: 33 min on one core, under 5 min on 16. A pattern ending in `/` selects a directory (`test/battle/move_effect/`). CI runs them in a workflow of its own (see *CI time*).
- *CI time:* with the move-effect tests in it, the Linux (32-bit) job took ~18 min (the Windows and sanitizer jobs ~5), with no output until the tests finished; 19b-4's ~5,000 more tests would have taken it to its 60-minute timeout. So, before 19b-4:
  1. *Done (both workflows now finish within ~9 min): a workflow of its own, split over machines* (`.github/workflows/upstream-tests.yml`): 4 machines, each building `pkmemerald-tests` and running its part of everything under `test/` with `pkmemerald-tests -j 4 --shard I/4` (machine I runs shards (I-1)·4 … I·4-1 of 16; every machine must use the same `-j`, and upstream allows 32 shards in all). Each machine builds its own copy: no artifacts or caches of build output (Phase 17). Run directly rather than through CTest, so the log shows results as they come; machine 1 also runs the runner's own CTest checks. The Linux job in `ci.yml` is back to the game alone. Locally, the 4 parts at once on 16 cores: under 5 min.
  2. *Done: a path filter:* that workflow runs only when `reference`, `platform/`, `tools/`, `cmake/`, `CMakeLists.txt` or the workflow itself change.
  3. *Only if still too slow:* a subset on pull requests and the full suite on `main` (or nightly).
  - Found on the way, an upstream runner bug: after a crash, the restarted runner replays the cost assignment over every earlier test, ignoring the filter, so with a filter the shards' costs drift apart and some tests run twice, others not at all (also on the GBA with Hydra; here 149 tests ran twice). `test/test_runner.c.patch` replays it over the filtered tests only.
- *19b-2 done:* all non-battle tests (`test/*.c`, `test/compression/`; 301) run in the test build, CTest and the upstream workflow (`test/`). All pass except 12 known GBA/host differences. What it took:
  - Test assets: `test/compression/` INCBINs images upstream's Makefile converts; `prepare_reference.py` asks for test sources' dependencies with `TEST=1` (`build/emerald-test/`).
  - Inline event scripts (`OVERWORLD_SCRIPT`, 128 uses): a host `include/test/overworld_script.h` (`platform/upstream_tests/include/`, linked into `host_include/test/`) puts the script in `.rodata` and takes its address with a `mov`, instead of ARM code. The script macros are host copies (`host_assemble.sh inc`: the ARM-to-x86 fixups, and `call`/`inc`/`nop`/`lock` renamed `host_*`, as they are defined for the whole file and would replace the x86 instructions of the C code; each script gets the upstream names back as aliases, removed after it).
  - Known GBA/host differences (`platform/upstream_tests/host_known_failures.c`, with reasons; reported as KNOWN_FAILING, and a listed test that passes is flagged): 9 benchmarks (`EXPECT_FASTER` times ARM code with GBA timers), and the 3 save block size tests (below).
  - Upstream test bugs the GBA hides, fixed in test patches: a 12-byte buffer for a 13-byte nickname (`test/pokemon.c.patch`; the host's stack protector caught it), FRLG's NULL map groups read (`test/text.c.patch`).
  - Port bugs (`known_crashes.md`): Day Care eggs from parents sharing a move (an upstream loop advancing the wrong index), the Illusion lookup outside battle. Also: `MgbaPrintf` formats with the game's own `mini_vsnprintf`, as upstream does, so `%S` (a game string) no longer reads past the end as a wide string.
- *Save layout (decided 2026-10-06: no GBA save compatibility):* `test/save.c` showed that the host's save blocks differ from the GBA's: SaveBlock1 15496 bytes (GBA: 15568), SaveBlock2 3848 (3884), SaveBlock3 1 (4); PokemonStorage matches. The GBA ABI (`-mabi=apcs-gnu`) rounds every struct's size up to a multiple of 4; x86 doesn't, so the padding inside and between structs differs. So GBA emulator saves don't load in the PC build, nor PC saves in an emulator. Matching the GBA layout would tie every saved struct to the GBA ABI and limit later changes, so the PC save format is its own: the same 128 KiB flash image, with the host's struct layout. Saves stay compatible between PC builds of the same upstream version (the played checkpoints); Phase 18's 64-bit build must keep that layout (its `u64` alignment note). The three `test/save.c` size tests stay on the known-differences list. *Later, maybe:* a converter between GBA and PC saves (it would decode each save block with one layout and re-encode it with the other).
- *19b-4 in progress, in batches (one pull request each):*
  - *Batch 1 done: `test/battle/ability/` and `test/battle/hold_effect/`* (437 files, ~2,050 tests) in the test build, CTest (`upstream-battle`, now all of `test/battle/` that is built) and the upstream workflow. All pass (137 are upstream's TO_DO, 8 upstream's own KNOWN_FAILING). On 16 cores: abilities 8 min, hold effects 1 min.
  - Port bug found (`known_crashes.md`): a double battle against two trainers overflowed a stack array in `BufferBattlePartyOrderBySide` (`party_menu.c.patch`); the host's stack protector aborted, in the game too.
  - A first unpatched run over all of `test/battle/` also crashed in 1v2 tests in `ai/` (`ai_doubles.c`, `ai_multi.c`: Explosion with Risky, Revival Blessing) and `battle_message.c`; likely the same bug, to check with their batch.
  - Next: the remaining directories (`ai/`, `move_effect_secondary/`, `move_flags/`, `form_change/`, `item_effect/`, `move_effects_combined/`, the small ones and the top-level files); check the upstream workflow's time as they come in.

### 19c — Visit every map (~1 day)

There are 939 maps (`reference/data/maps/`). A headless run that warps to each one in turn (through the game's own warp code: `SetWarpDestination` + the map loading callbacks), runs a few frames, and moves on, catching crashes in map loading, map scripts, and tileset/event data that playtesting would take weeks to reach. Under the sanitizers of 19a it also catches the silent memory errors.

**Edits**
- A test executable (or a `--visit-maps` debug flag) that steps through every map group and number, with a timeout per map.
- In CI, with the sanitizer build.

**Done when** every map loads and runs its first frames without a crash or sanitizer report, or is listed with a reason (maps that are unreachable in normal play may need special setup).

**Status: not started — next.** Order: 19a, then 19b, then 19c. Each is its own pull request.

---

## Effort estimate (single experienced C dev)

| Phase | Effort | Risk |
|------:|------:|------|
| 0–2   | 1–2 days | low |
| 3     | 1 week  | medium — surfaces all portability bugs |
| 4–5   | 1–2 days | low |
| 6–7   | 2–3 days | low |
| 8     | 1–2 days | low |
| 9     | 1–2 weeks | medium — LZ decomp, palette math, large asset surface |
| 10    | 2–3 weeks | **high** — affine BG, blending, mode 7 are subtle |
| 11    | 1–2 weeks | medium |
| 12    | 1–2 days | low |
| 13    | 2–3 weeks | **high** — voice groups, song format conversion, timing |
| 14    | 1 week (verification) | low |
| 15    | 2–3 weeks | medium |
| 16    | 1–2 weeks | medium (optional) |
| 17    | 1–2 weeks | low |
| 18    | 3–5 weeks (1 week for the Linux/Windows shortcut) | medium — layout mismatches are silent unless the build-time checks are in place |
| 19    | 4–6 days (19a ~1 day, 19b 2–4 days, 19c ~1 day) | medium — the upstream test runner depends on GBA specifics; some tests may need GBA-only behaviour |

**Realistic total**: 3–5 months to a single-player build of the full Hoenn region. With multiplayer, plan on 4–6 months. The first playable milestone (walk around one map) is reachable in **~4–6 weeks** with focused work.

---

## Risks & open questions

1. **Asset legality.** `reference/data/` and the baseline ROM contain Nintendo's IP. Distributing a port that ships these assets is the same legal posture as the existing `pokeemerald-expansion` repo, but a *commercial* PC release isn't viable without original assets. *Decided (2026-10-06): source only.* No binary downloads, releases or CI artifacts; users build the game themselves (Phase 17).
2. **Voice groups and battle SFX.** *Resolved:* the game's own M4A engine runs (its assembly translated to C) on emulated sound hardware, so nothing is lost (Phase 13).
3. **Battle animations & special effects.** Some use window blending, mosaic, and capture-effect tricks that don't map 1:1 to SDL2. The "blend" register in particular is non-trivial. Plan for one extra week of renderer polish during Phase 15.
4. **Link protocol.** RFU is undocumented; we only have a decomp of it. The UDP-replacement will need a custom discovery layer. (See Phase 16.)
5. **Upstream drift.** `reference/` is a submodule on `upcoming`. Plan a quarterly merge: rebase the platform layer, re-run asset pipeline. The cleanest mitigation is to keep our diff in `platform/` and `tools/` only, and never edit `reference/src/`.

---

## Building and running

See [README.md](README.md): `cmake -S . -B build && cmake --build build -j`, then `./build/pkmemerald` (`-h` for options) and `ctest --test-dir build`.
