#!/bin/bash
# Builds the distributable Windows package: the engine (no game code) + the builder that turns
# each user's own ROMs into vt_game.dll. Nothing derived from the game is included.
#
#   LLVM_MINGW=/opt/llvm-mingw-20251104-ucrt-ubuntu-22.04-x86_64 vt/tools/package_windows.sh [outdir]
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/dist}"
BUILD="$ROOT/build-win-engine"
: "${LLVM_MINGW:?set LLVM_MINGW to the llvm-mingw toolchain folder}"
export LLVM_MINGW
# Flycast includes some Windows headers with their MSVC capitalisation: add case aliases
INC="$LLVM_MINGW/generic-w64-mingw32/include"
for h in D3Dcompiler.h:d3dcompiler.h Commctrl.h:commctrl.h Windows.h:windows.h Shellapi.h:shellapi.h \
	Psapi.h:psapi.h SDKDDKVer.h:sdkddkver.h TlHelp32.h:tlhelp32.h WinSock2.h:winsock2.h ShlObj.h:shlobj.h; do
	[ -e "$INC/${h%%:*}" ] || ln -s "${h##*:}" "$INC/${h%%:*}"
done

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE="$ROOT/vt/tools/mingw-toolchain.cmake" \
	-DVNBA_PRODUCT=ON -DUSE_BREAKPAD=OFF -DUSE_LUA=OFF -DUSE_OPENMP=OFF
ninja -C "$BUILD"

PKG="$OUT/VirtuaTennis-Recomp"
rm -rf "$PKG"
mkdir -p "$PKG/builder" "$PKG/roms" "$PKG/data"
cp "$BUILD/VirtuaTennis-Recompiled.exe" "$PKG/"
cp "$LLVM_MINGW/x86_64-w64-mingw32/bin/libwinpthread-1.dll" "$PKG/"
cp "$ROOT/vt/builder/CONSTRUIR_JUEGO.bat" "$ROOT/vt/builder/BUILD_GAME.bat" "$PKG/"
cp "$ROOT/vt/builder/build_game.ps1" "$ROOT/vt/builder/Makefile" "$ROOT/vt/builder/seeds_vtennis.txt" "$ROOT/vt/builder/seeds_vtenis2c.txt" "$PKG/builder/"
while read -r f; do
	mkdir -p "$PKG/builder/sdk/$(dirname "$f")"
	cp "$ROOT/$f" "$PKG/builder/sdk/$f"
done < "$ROOT/vt/builder/sdk_files.txt"
cp "$ROOT/vt/docs/LEEME.txt" "$ROOT/vt/docs/README.txt" "$PKG/"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt"
echo "Copy vtennis.zip and/or vtenis2c.zip here / Copia aquí vtennis.zip y/o vtenis2c.zip" > "$PKG/roms/PUT_vtennis.zip_vtenis2c.zip_HERE.txt"
echo "Copy naomi.zip here (first start only) / Copia aquí naomi.zip (sólo el primer arranque)" > "$PKG/data/PUT_naomi.zip_HERE.txt"
(cd "$OUT" && rm -f VirtuaTennis-Recomp-win64.zip && zip -q -r -9 VirtuaTennis-Recomp-win64.zip VirtuaTennis-Recomp)
echo "Package: $OUT/VirtuaTennis-Recomp-win64.zip"
