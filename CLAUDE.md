# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

A PC port (SDL2, CMake) of pokeemerald-expansion. [CONTRIBUTING_PC.md](CONTRIBUTING_PC.md) is the authoritative guide to how the port is organised; [PLAN.md](PLAN.md) holds the phased plan and current status (read its status table before starting phase work); [known_crashes.md](known_crashes.md) logs every crash fix.

## Ground rules

- **`reference/` (the upstream submodule) is never edited.** Changes to upstream `.c` files go in `platform/patches/<file>.c.patch` (upstream `test/` files: `platform/patches/test/<file>.c.patch`), applied to copies in the build directory. Prefer a host-side fix in `platform/` when one is possible.
- Mark every change in a patch with a `PC port:` comment explaining why; wrap it in `#ifdef HOST_BUILD` if the GBA behaviour must stay.
- Every crash fix gets an entry in `known_crashes.md` (what crashed, why, what changed), committed with the fix.
- Work on a branch and open a PR; never push to `main`.
- **Source only:** nothing built is published. No CI artifacts, releases, or caches holding compiled game code (the build contains Nintendo assets and the repo is public). The only CI cache is upstream's generated assets (`tools/ci_reference_cache.sh`).
- GBA save compatibility was dropped on purpose: the PC save uses its own struct layout.

## Commands

```sh
git submodule update --init
cmake -S . -B build -DPKM_WERROR=ON        # PKM_WERROR: warnings in platform/ are errors, as in CI
cmake --build build -j
ctest --test-dir build                     # all tests; -R <regex> for one, e.g. -R test_save
cmake --build build --target reference-prepare   # regenerate upstream tools/assets after upstream changes
tools/check_patches.sh                     # every patch must apply exactly (after updating upstream)
```

- Sanitizer build (Linux): a separate build dir with `-DPKM_SANITIZE=address,undefined`.
- Windows: cross-build with MinGW: `tools/fetch_sdl2_mingw.sh`, then `cmake -S . -B build-win --toolchain cmake/mingw-i686.cmake`, and run the tests under Wine with `ctest --test-dir build-win`.
- Upstream's test suite: configure with `-DPKM_UPSTREAM_TESTS=ON` (use a separate dir such as `build-tests`), build target `pkmemerald-tests`, then run `build-tests/pkmemerald-tests [-j N] [--shard I/M] [--draw] [PATTERN]`. Frames aren't drawn unless `--draw` is given (drawing made the suite ~15× slower); the summary ends with each shard's time and the slowest tests. PATTERN can be a file (`test/fpmath.c`), a directory ending in `/` (`test/battle/move_effect/`), a test-name prefix, or `*infix`. `PKM_TESTS_NO_FORK=1` runs everything in one process, for gdb.
- Repeatable game runs: `--monkey SEED[@FRAME]`, `-i SCRIPT` (input at given frames), `-f N` (stop after N frames), `-o FILE` (save the last frame), `--fast`, and `-s DIR` (use a copy of a save directory). The `monkey-*` CTest entries use these.
- Making a patch:
  ```sh
  patch -o /tmp/foo.c reference/src/foo.c platform/patches/foo.c.patch   # or cp if no patch yet
  $EDITOR /tmp/foo.c
  diff -u --label a/src/foo.c --label b/src/foo.c reference/src/foo.c /tmp/foo.c > platform/patches/foo.c.patch
  ```

## Architecture

**How an upstream file is built** (`CMakeLists.txt`, mirroring `reference/Makefile`):
1. `tools/prepare_reference.py` builds upstream's host tools (preproc, gbagfx, mapjson, ...), generated headers and converted graphics into `reference/build/`. This runs once, through the stamp target `reference-prepare`.
2. The optional `platform/patches/<f>.c.patch` is applied to a copy.
3. `tools/host_preprocess.sh` runs host cpp and upstream's preproc to produce `<f>.i`.
4. The host compiler builds it into the `pkmemerald-core` static library.

All of `reference/src/*.c` is built except `REFERENCE_SRC_EXCLUDE`, the GBA-only files (flash, wireless, multiboot), which have replacements in `platform/src/`. The assembly *data* (`data/*.s`, songs) is assembled for the host by `tools/host_assemble.sh`; ARM code `.s` files are never built, and functions upstream writes in assembly get C versions (e.g. `platform/src/host_gba_misc.c`).

**Host include tree:** upstream includes headers with quotes, so the build dir holds a symlink mirror of `reference/include/` in which `gba/` points to `platform/include/gba/`. The GBA registers, VRAM, BIOS calls and so on there map onto host memory. `platform/include/platform/` holds the interfaces between platform files.

**32-bit, fixed addresses:** the game assumes 32-bit pointers (u32 task args, static asserts on struct sizes), so the build is `-m32` and non-PIE. The game tells code from data by address (tagged script pointers in `scrcmd.c`/`script.c`), so code must sit in the GBA ROM window, 0x08000000–0x0A000000. That is Linux's non-PIE default; on Windows it comes from `--image-base 0x08000000`. A 64-bit build is the future Phase 18.

**Platform layer** (`platform/src/`): `host_main.c` is the entry point; it also has rendering (`host_render.c`), the sound hardware (`host_audio.c`) and m4a engine (`m4a_engine.c`), flash saves (`host_flash.c`), input, settings (`host_config.c`) and crash handlers. OS-specific files end in `_posix`/`_linux`/`_win32`, SDL-specific ones in `_sdl2`.

**Tests:**
- `platform/tests/`: each `.c` file is its own executable; there are also Python tests for the save tools in `platform/tools/`. They run headless.
- The `data-abi-matches-gba` test needs the ARM toolchain. It checks the host-assembled data against upstream's GBA build, byte for byte.
- `platform/upstream_tests/` runs upstream's own test framework on the host:
  - `host_test_runner.c` forks shards and restarts them after a crash.
  - Battle tests run on a ucontext stack in `HOST_TEST_RAM` (mapped at 0x02000000), because the framework stores addresses in 27/28-bit fields.
  - `host_known_failures.c` lists the expected GBA/host differences (benchmarks, save layout).
  - Host overrides of upstream test headers are in `platform/upstream_tests/include/test/`.

**CI:**
- `.github/workflows/ci.yml` builds and tests Linux, Linux with sanitizers, and Windows under Wine.
- `.github/workflows/upstream-tests.yml` runs upstream's suite on one machine (`--shard` can split it over several if it grows). It runs only when `reference`, `platform/`, `tools/`, `cmake/` or `CMakeLists.txt` change.
