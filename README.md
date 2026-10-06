# pkmemerald

A PC port of [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion).

Work in progress. See [PLAN.md](PLAN.md) for the phased port plan and current status, and [CONTRIBUTING_PC.md](CONTRIBUTING_PC.md) for how the port is organised, how to change upstream code, and how to update to a newer upstream.

**Source only.** The built game contains Nintendo's assets (graphics, music and text from `reference/`), so this project provides no binaries: build it yourself with the instructions below.

## Layout

| Path        | What it is |
|-------------|------------|
| `reference/` | Upstream pokeemerald-expansion, from [our fork](https://github.com/Yvonne-Aizawa/pokeemerald-expansion) (git submodule). **Not edited**: host changes go in `platform/`. |
| `platform/` | The PC port: GBA hardware stubs (`include/gba/`), host implementations (`src/`), build-time patches to upstream sources (`patches/`), and smoke tests (`tests/`). |
| `tools/`    | Host build helpers that drive upstream's tools (preproc, gbagfx, mapjson, ...). |

## Building

### Linux

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

This makes an optimised build with debug symbols (`RelWithDebInfo`); add `-DCMAKE_BUILD_TYPE=Debug` to the first `cmake` for an unoptimised one. When changing the port's own code (`platform/`), add `-DPKM_WERROR=ON`: warnings there are then errors, as in CI. (Upstream's code in `reference/` keeps its warnings; we don't edit it.)

The first build also builds upstream's tools and converts its graphics into `reference/build/` (ignored by upstream's `.gitignore`). After changing upstream graphics or data, re-run that step with:

```sh
cmake --build build --target reference-prepare
```

To install it for your user (the game data is built into the executable; it needs the 32-bit SDL2 at run time):

```sh
cmake --install build --prefix ~/.local
```

This installs `~/.local/bin/pkmemerald` and a desktop launcher. Installed copies (and packages) leave out the debug info but keep the symbol table, so crash reports still name functions. `cmake --build build --target package` makes the same as a `.tar.gz`, for copying to your own machines.

### Windows

The Windows version (32-bit, like the Linux one) is cross-compiled on Linux with MinGW-w64; upstream's tools still run on the Linux machine. Besides the Linux requirements above (except the 32-bit SDL2), install `gcc-mingw-w64-i686`. Then:

```sh
tools/fetch_sdl2_mingw.sh                                   # SDL2 for MinGW, into deps/
cmake -S . -B build-win --toolchain cmake/mingw-i686.cmake
cmake --build build-win -j
cmake --build build-win --target package                    # pkmemerald-*-win32.zip
```

The zip holds `pkmemerald.exe` and `SDL2.dll`; unpack it anywhere on the Windows machine and run the `.exe`. It opens no console window: its messages go to `pkmemerald-log.txt` in the save directory, or, with `pkmemerald.exe --console`, to the console it was started from (or a new one). With Wine installed, `ctest --test-dir build-win` runs the tests under it (and `wine build-win/pkmemerald.exe` runs the game).

### Settings

Settings are in `pkmemerald.ini` in the save directory (below), created with comments and the defaults the first time the game starts: window scale, fullscreen, sound on/off, exact or smoothed sound, and the keyboard layout (keys for each GBA button). The game never rewrites it, so edits stay; mistakes are reported in the log and that setting keeps its default. Command-line flags override it for one run (`pkmemerald --help`); delete the file to get the defaults back. F11 or Alt+Enter switch between the window and fullscreen while playing.

### Saves and crash reports

Saves go to `pkmemerald.sav` (a 128 KiB flash image, as on the GBA, but with the PC build's own data layout: saves from a GBA emulator don't load, and the other way round) in the save directory: `~/.local/share/pkmemerald/` on Linux, `%APPDATA%\pkmemerald\` on Windows, or `saves/` in the current directory if a save is already there (where saves went before). `-s DIR` uses another directory.

If the game crashes, it prints a report (signal or exception, crashing function, call stack, and the game's current callbacks, i.e. which screen it was on) and also writes it to a new file in the `crashes` folder of the save directory, named after the date and time of the crash (for example `crashes/2026-10-06_19-10-32_pkmemerald-crash.txt`). Earlier reports are kept. Please include the file when reporting a crash.

### Running

`./build/pkmemerald` runs the game in a window (`-h` for options). It is playable, with sound, but early: expect bugs. `--mute` turns the sound off; `-a FILE` records it to a WAV file instead; the sound is the GBA's exact output; `--smooth-sound` interpolates and low-passes it instead. Like on the GBA, the game starts in mono: switch "Sound" to Stereo in the in-game Options. Default controls: arrows, Z = A, X = B, Enter = Start, Backspace = Select, Shift = L, Ctrl = R (change them in `pkmemerald.ini`), or a gamepad. Run the tests with:

```sh
ctest --test-dir build
```
