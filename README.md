# pkmemerald

A PC port of [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion).

Work in progress. See [PLAN.md](PLAN.md) for the phased port plan and current status.

**Source only.** The built game contains Nintendo's assets (graphics, music and text from `reference/`), so this project provides no binaries: build it yourself with the instructions below.

## Layout

| Path        | What it is |
|-------------|------------|
| `reference/` | Upstream pokeemerald-expansion (git submodule). **Not edited**: host changes go in `platform/`. |
| `platform/` | The PC port: GBA hardware stubs (`include/gba/`), host implementations (`src/`), build-time patches to upstream sources (`patches/`), and smoke tests (`tests/`). |
| `tools/`    | Host build helpers that drive upstream's tools (preproc, gbagfx, mapjson, ...). |

## Building

Requirements (Debian/Ubuntu names):

- `cmake` (3.20+), `gcc`, `g++`, `make`, `python3`, `patch`
- `gcc-multilib`: the port builds 32-bit, because the game assumes 32-bit pointers
- `libsdl2-dev:i386`: SDL2, in its 32-bit version
- `libpng-dev`: for upstream's graphics tool

```sh
git submodule update --init
cmake -S . -B build
cmake --build build -j
```

This makes an optimised build with debug symbols (`RelWithDebInfo`); add `-DCMAKE_BUILD_TYPE=Debug` to the first `cmake` for an unoptimised one.

The first build also builds upstream's tools and converts its graphics into `reference/build/` (ignored by upstream's `.gitignore`). After changing upstream graphics or data, re-run that step with:

```sh
cmake --build build --target reference-prepare
```

To install it for your user (the game data is built into the executable; it needs the 32-bit SDL2 at run time):

```sh
cmake --install build --prefix ~/.local
```

This installs `~/.local/bin/pkmemerald` and a desktop launcher. `cmake --build build --target package` makes the same as a `.tar.gz`, for copying to your own machines.

Saves go to `pkmemerald.sav` (a standard 128 KiB GBA flash save) in the save directory: `~/.local/share/pkmemerald/`, or `saves/` in the current directory if a save is already there (where saves went before). `-s DIR` uses another directory.

If the game crashes, it prints a report (signal, crashing function, call stack, and the game's current callbacks, i.e. which screen it was on) and also writes it to `pkmemerald-crash.txt` in the save directory. Please include that file when reporting a crash.

`./build/pkmemerald` runs the game in a window (`-h` for options). It is playable, with sound, but early: expect bugs. `--mute` turns the sound off; `-a FILE` records it to a WAV file instead; the sound is the GBA's exact output; `--smooth-sound` interpolates and low-passes it instead. Like on the GBA, the game starts in mono: switch "Sound" to Stereo in the in-game Options. Controls: arrows, Z = A, X = B, Enter = Start, Backspace = Select, Shift = L, Ctrl = R, or a gamepad. Run the tests with:

```sh
ctest --test-dir build
```
