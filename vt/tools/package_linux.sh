#!/bin/bash
# Builds the distributable Linux x86_64 package: the engine (no game code) + the builder that
# turns each user's own ROMs into libvt_game.so. Nothing derived from the game is included.
#   vt/tools/package_linux.sh [outdir]
# Build dependencies (Ubuntu): build-essential cmake ninja-build libsdl2-dev libudev-dev libpulse-dev
#   libasound2-dev libgl-dev zlib1g-dev  (libsdl2-dev only to pull SDL's X11/Wayland headers)
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/dist}"
BUILD="$ROOT/build-linux-engine"

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DVNBA_PRODUCT=ON -DUSE_BREAKPAD=OFF -DUSE_LUA=OFF \
	-DUSE_HOST_SDL=OFF -DUSE_HOST_LIBZIP=OFF -DUSE_LIBAO=OFF -DUSE_OPENMP=OFF \
	-DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
ninja -C "$BUILD"

# Portability check: the binary may only depend on libraries every desktop Linux has
# (SteamOS, Arch, Fedora, Debian...). SDL and libzip are linked statically, libcurl/libao not used.
ALLOWED='^(linux-vdso|libc|libm|libdl|libpthread|librt|libgcc_s|libstdc\+\+|libz|libzstd|libGL|libOpenGL|libGLX|libEGL|libudev|libpulse|libasound|libpng16|ld-linux-x86-64)\.so'
BAD=$(objdump -p "$BUILD/VirtuaTennis-Recompiled" | awk '/NEEDED/{print $2}' | grep -Ev "$ALLOWED" || true)
objdump -p "$BUILD/VirtuaTennis-Recompiled" | awk '/NEEDED/{print "  needs " $2}'
if [ -n "$BAD" ]; then echo "ERROR: non-portable dependencies: $BAD"; exit 1; fi
GLIBC=$(objdump -T "$BUILD/VirtuaTennis-Recompiled" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "  highest glibc symbol: $GLIBC"

PKG="$OUT/VirtuaTennis-Recomp"
rm -rf "$PKG"
mkdir -p "$PKG/builder" "$PKG/roms" "$PKG/data"
cp "$BUILD/VirtuaTennis-Recompiled" "$PKG/"
strip "$PKG/VirtuaTennis-Recompiled"
cp "$ROOT/vt/builder/build_game.sh" "$ROOT/vt/builder/VirtuaTennis.sh" "$PKG/"
chmod +x "$PKG/build_game.sh" "$PKG/VirtuaTennis.sh" "$PKG/VirtuaTennis-Recompiled"
cp "$ROOT/vt/builder/seeds_vtennis.txt" "$ROOT/vt/builder/seeds_vtenis2c.txt" "$PKG/builder/"
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
