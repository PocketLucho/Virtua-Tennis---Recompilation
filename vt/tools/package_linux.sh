#!/bin/bash
# Builds the distributable Linux x86_64 package: the engine (no game code) + the builder that
# turns each user's own ROMs into libvt_game.so. Nothing derived from the game is included.
#   vt/tools/package_linux.sh [outdir]
# Build dependencies (Ubuntu): build-essential cmake ninja-build libsdl2-dev libcurl4-openssl-dev
#   libudev-dev libpulse-dev libasound2-dev libgl-dev libzip-dev zlib1g-dev
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/dist}"
BUILD="$ROOT/build-linux-engine"

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DVNBA_PRODUCT=ON -DUSE_BREAKPAD=OFF -DUSE_LUA=OFF
ninja -C "$BUILD"

PKG="$OUT/VirtuaTennis-Recomp"
rm -rf "$PKG"
mkdir -p "$PKG/builder" "$PKG/roms" "$PKG/data"
cp "$BUILD/VirtuaTennis-Recompiled" "$PKG/"
strip "$PKG/VirtuaTennis-Recompiled"
cp "$ROOT/vt/builder/build_game.sh" "$PKG/"
cp "$ROOT/vt/builder/Makefile.linux" "$ROOT/vt/builder/seeds_vtennis.txt" "$ROOT/vt/builder/seeds_vtenis2c.txt" "$PKG/builder/"
while read -r f; do
	mkdir -p "$PKG/builder/sdk/$(dirname "$f")"
	cp "$ROOT/$f" "$PKG/builder/sdk/$f"
done < "$ROOT/vt/builder/sdk_files.txt"
cp "$ROOT/vt/docs/LEEME-linux.txt" "$ROOT/vt/docs/README-linux.txt" "$PKG/"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt"
echo "Copy vtennis.zip and/or vtenis2c.zip here / Copia aquí vtennis.zip y/o vtenis2c.zip" > "$PKG/roms/PUT_vtennis.zip_vtenis2c.zip_HERE.txt"
echo "Copy naomi.zip here (first start only) / Copia aquí naomi.zip (sólo el primer arranque)" > "$PKG/data/PUT_naomi.zip_HERE.txt"
(cd "$OUT" && rm -f VirtuaTennis-Recomp-linux-x64.tar.gz && tar -czf VirtuaTennis-Recomp-linux-x64.tar.gz VirtuaTennis-Recomp)
echo "Package: $OUT/VirtuaTennis-Recomp-linux-x64.tar.gz"
