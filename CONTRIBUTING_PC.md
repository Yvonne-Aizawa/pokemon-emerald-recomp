# Contributing to the PC port

How the port is put together, where changes go, and how to update to a newer
upstream. Building and running are in [README.md](README.md); the phased plan
and status are in [PLAN.md](PLAN.md).

## Ground rules

- **No completed game builds.** The built game contains Nintendo's assets
  (graphics, music and text from `reference/`). Nothing built is published
  anywhere: no GitHub Releases, no download links, no CI artifacts, and no CI
  cache that holds compiled game code (`.github/workflows/ci.yml` explains the
  one cache it has). Packages (`--target package`) are for your own machines.
- **Reviewed played-save JSON fixtures are allowed** in `platform/tests/fixtures/`.
  This is an explicit exception for test input data, containing all original save
  bytes. Generated `.sav` files stay in build directories and must never be
  uploaded as CI artifacts or cached. Local `test-saves/` remains ignored.
- **`reference/` is never edited.** It is upstream
  ([pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion))
  as a git submodule. Changes to upstream code go in `platform/patches/`
  (below), applied to copies at build time.
- **Changes go through pull requests**, not straight to `main`; CI must pass.
- **A crash fix is documented in [known_crashes.md](known_crashes.md)**, in
  the same commit as the fix: what crashed, why, and what changed.

## Where things live

| Path | What goes there |
|------|-----------------|
| `platform/src/` | The host side: display (`host_render.c`), sound hardware (`host_audio.c`) and engine (`m4a_engine.c`), saves (`host_flash.c`), input, the main loop, settings, crash handler. OS-specific files end in `_posix.c`/`_linux.c` or `_win32.c`; SDL-specific ones in `_sdl2.c`. |
| `platform/include/gba/` | Replacements for upstream's GBA hardware headers: registers, VRAM and the rest point into host memory. Upstream code sees these instead of `reference/include/gba/`. |
| `platform/include/platform/` | Interfaces between the platform files. |
| `platform/patches/` | Changes to upstream `.c` files, one `<file>.c.patch` per file. |
| `platform/tests/` | Tests; each `.c` file is its own test executable. |
| `tools/` | Build helpers driving upstream's tools (preprocessing, assembling the data, preparing the assets), and CI/maintenance scripts. |
| `cmake/` | The Windows (MinGW) toolchain file. |

The game data is built into the executable: upstream's own tools convert it
(`tools/prepare_reference.py`), and the game's assembly data is assembled for
the host (`tools/host_assemble.sh`).

## Changing upstream code: patches

Prefer a host-side fix in `platform/` (a header in `platform/include/gba/`, a
host implementation of a function upstream writes in assembly). Patch an
upstream file only when its C code itself is the problem on a PC, typically:

- something the GBA tolerates but a PC doesn't: reading through NULL or an
  uninitialized pointer, reading past an array (see `known_crashes.md`);
- different hardware behaviour: shifts by 32 or more, address tricks such as
  the tagged script pointers in `scrcmd.c`;
- ARM assembly in a C file (`random.c`).

Keep patches small. Mark each change with a `PC port:` comment saying why, and
wrap it in `#ifdef HOST_BUILD` when the original must stay as it is for the
GBA build. To make or change a patch:

```sh
# start from upstream's file, with the existing patch if there is one
patch -o /tmp/foo.c reference/src/foo.c platform/patches/foo.c.patch   # or: cp reference/src/foo.c /tmp/foo.c
$EDITOR /tmp/foo.c
diff -u --label a/src/foo.c --label b/src/foo.c reference/src/foo.c /tmp/foo.c > platform/patches/foo.c.patch
```

The build applies it to a copy in the build directory; a new patch file is
picked up on the next build.

## Writing code

- C17 with GNU extensions, in the style of the surrounding file.
- Configure with `-DPKM_WERROR=ON` while working: warnings in `platform/` are
  then errors, as in CI. (Upstream's code keeps its own warnings.)
- The game is built 32-bit and assumes 32-bit pointers (PLAN.md, Phase 18).
- Upstream headers are included with quotes (`#include "global.h"`); they
  resolve through the host include tree in the build directory, which swaps in
  `platform/include/gba/`.
- Add a test in `platform/tests/` for anything testable without playing. Tests
  run headless (SDL's dummy video and audio drivers).
- Changes to Windows-specific code: build with the MinGW toolchain and run the
  tests under Wine (README.md, "Windows"). CI does both anyway.

Finding memory errors (Linux): configure a separate build with
`-DPKM_SANITIZE=address,undefined`. AddressSanitizer and UBSan then report
reads through NULL or past an array where they happen, even when nothing
crashes; the GBA tolerates those, so upstream has some (see
`known_crashes.md`). `ctest` runs the tests and the monkey runs below under
them; CI does the same. The game's own heap is checked too (freed and unused
heap memory is off-limits); `PKM_ASAN_HEAP=0` turns that part off.

Upstream's own test suite (Linux, in progress, PLAN.md Phase 19b): configure
with `-DPKM_UPSTREAM_TESTS=ON`, then `build/pkmemerald-tests [PATTERN]` runs
it (a test file such as `test/fpmath.c`, a test name prefix, or `*infix`);
`ctest` runs the parts that work so far.

Useful for checking changes in the running game:

- `--monkey SEED[@FRAME]` plays with pseudo-random input, the same for the
  same seed; the `monkey-*` tests use it, from boot and after a quick start
  (`-i 600:a,1300+10:select`: Select on the title screen). A seed that finds
  a bug reproduces it.
- `-i SCRIPT` presses buttons at given frames, `-f N` stops after N frames,
  `-o FILE` saves the last frame, `-a FILE` records the sound, and `--fast`
  runs unpaced. Together they give repeatable runs: comparing the frame or
  sound from two builds shows whether a change altered anything.
- Use a copy of a save with `-s DIR`, so tests don't touch your own.
- Crash reports go to `crashes/YYYY-MM-DD_HH-MM-SS_pkmemerald-crash.txt` in
  the save directory (local time, one file per crash), with the crashing
  function, call stack and current screen.

## Updating upstream

`reference/` is pinned to one upstream commit; updating it is a pull request
of its own.

1. **Move the submodule:**
   `git -C reference fetch && git -C reference checkout <commit>`
   (a commit on upstream's `master`).
2. **Check the patches:** `tools/check_patches.sh`. Redo any that fail by the
   recipe above, starting from the new upstream file. Those that apply only
   inexactly still build, but check where their hunks land and regenerate them
   so they apply exactly.
3. **New or removed upstream files:** every `reference/src/*.c` is built
   except those in `REFERENCE_SRC_EXCLUDE` (`CMakeLists.txt`), which are
   GBA-only (flash chips, the wireless adapter, multiboot) and replaced in
   `platform/src/`. A new GBA-only file belongs on that list, with its
   replacement.
4. **Regenerate the assets and build:**
   `cmake --build build --target reference-prepare`, then
   `cmake --build build`. Undefined symbols at link time usually mean
   upstream added a function in assembly or the linker script, which needs a
   host version (see `platform/src/host_gba_misc.c`).
5. **Test:** `ctest --test-dir build`. Install the ARM toolchain
   (`gcc-arm-none-eabi`, `libnewlib-arm-none-eabi`) so `data-abi-matches-gba`
   runs: it checks that the assembled game data and the structs read from it
   match upstream's GBA build byte for byte.
6. **Play:** the intro, the truck, Littleroot, Route 101, a wild battle,
   catching, the Pokémon Center PC, the Pokédex, and saving and continuing.
   Fix crashes as patches, and log them in `known_crashes.md`.
7. **Open the pull request** with the submodule update, the patch changes and
   any new host code together; CI builds and tests Linux and Windows.

## CI

`.github/workflows/ci.yml` builds and tests the Linux version and the Windows
version (cross-compiled, tests under Wine) on every push to `main` and every
pull request, with warnings as errors in `platform/`. Changes that touch only
Markdown files skip it. It caches only what upstream's sources generate in
`reference/` (`tools/ci_reference_cache.sh`, which refuses anything that looks
like a build); the game is compiled from scratch every run, and nothing is
uploaded.
