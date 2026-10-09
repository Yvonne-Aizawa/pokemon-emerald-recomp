#!/usr/bin/env bash
#
# tools/fetch_sdl2_mingw.sh
#
# Download SDL2's official MinGW development package (headers, import
# libraries, SDL2.dll) for building the Windows version, and unpack it into
# deps/ (ignored by git). The archive is checked against a known SHA-256.
#
#   tools/fetch_sdl2_mingw.sh
#   cmake -S . -B build-win --toolchain cmake/mingw-i686.cmake
#
# The toolchain file finds the package in deps/ by itself. The package has
# both i686 and x86_64 builds, so cmake/mingw-x86_64.cmake uses it too.

set -euo pipefail

version=2.32.10
sha256=83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2
name=SDL2-devel-$version-mingw.tar.gz
url=https://github.com/libsdl-org/SDL/releases/download/release-$version/$name

root=$(cd "$(dirname "$0")/.." && pwd)
deps=$root/deps
dest=$deps/SDL2-$version

if [[ -d $dest ]]; then
    echo "SDL2 $version already in $dest"
    exit 0
fi

mkdir -p "$deps"
tmp=$(mktemp -d "$deps/.fetch.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

echo "Downloading $url"
curl -fL --retry 3 -o "$tmp/$name" "$url"
echo "$sha256  $tmp/$name" | sha256sum -c --quiet -
tar -xzf "$tmp/$name" -C "$tmp"
mv "$tmp/SDL2-$version" "$dest"
echo "SDL2 $version unpacked into $dest"
