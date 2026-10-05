# pkmemerald

A PC port of [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion).

Work in progress. See [PLAN.md](PLAN.md) for the phased port plan and current status.

## Layout

| Path        | What it is |
|-------------|------------|
| `refrence/` | Upstream pokeemerald-expansion (git submodule). **Not edited**: host changes go in `platform/`. |
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

The first build also builds upstream's tools and converts its graphics into `refrence/build/` (ignored by upstream's `.gitignore`). After changing upstream graphics or data, re-run that step with:

```sh
cmake --build build --target refrence-prepare
```

Saves go to `saves/pkmemerald.sav` (a standard 128 KiB GBA flash save; `-s DIR` to use another directory).

`./build/pkmemerald` runs the game in a window (`-h` for options). It is playable but silent, and early: expect bugs. Controls: arrows, Z = A, X = B, Enter = Start, Backspace = Select, Shift = L, Ctrl = R, or a gamepad. Run the tests with:

```sh
ctest --test-dir build
```
