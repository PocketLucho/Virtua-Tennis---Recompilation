#!/bin/bash
# VIRTUA TENNIS - RECOMP : builder (Linux)
# Generates the recompiled game code from YOUR Virtua Tennis / Virtua Tennis 2 ROMs and compiles it
# into libvt_game.so.
# Genera el código recompilado a partir de TUS ROMs y lo compila en libvt_game.so.
# Needs / necesita: g++ and make  (Debian/Ubuntu: sudo apt install build-essential)
set -e
cd "$(dirname "$(readlink -f "$0")")"
say() { printf '\n\033[36m== %s\033[0m\n   %s\n' "$1" "$2"; }
fail() { printf '\n\033[31mERROR: %s\n       %s\033[0m\n' "$1" "$2"; exit 1; }

say "VIRTUA TENNIS - RECOMP: construcción del código de los juegos" "VIRTUA TENNIS - RECOMP: building the game code"
games=()
for id in vtennis vtenis2c; do
	{ [ -f "roms/$id.zip" ] || [ -f "$id.zip" ]; } && games+=("$id")
done
[ ${#games[@]} -gt 0 ] || fail "No se encuentra ningún juego. Copia vtennis.zip y/o vtenis2c.zip en la carpeta 'roms'." "No game found. Copy vtennis.zip and/or vtenis2c.zip to the 'roms' folder."
command -v "${CXX:-g++}" >/dev/null || fail "Falta el compilador: sudo apt install build-essential" "Compiler missing: sudo apt install build-essential"
command -v make >/dev/null || fail "Falta make: sudo apt install build-essential" "make missing: sudo apt install build-essential"
echo "   ${games[*]}"

rm -rf builder/gen builder/obj && mkdir -p builder/gen builder/obj
dumps=""
for id in "${games[@]}"; do
	say "Generando el código C++ a partir de $id.zip..." "Generating C++ code from $id.zip..."
	VNBA_AOT=1 VNBA_START_GAME=$id VNBA_AOT_DUMP=builder/gen/dump_$id.cpp VNBA_AOT_STATIC=builder/seeds_$id.txt \
	VNBA_AOT_STATIC_PTR=1 VNBA_AOT_STATIC_SZ1=1 VNBA_AOT_STATIC_FUNCS=1 ./VirtuaTennis-Recompiled || true
	[ "$(stat -c %s builder/gen/dump_$id.cpp 2>/dev/null || echo 0)" -gt 1000000 ] || fail "La generación de $id ha fallado. La primera vez hace falta la BIOS naomi.zip en la carpeta 'data'." "Generation of $id failed. The first time, the NAOMI BIOS naomi.zip is needed in the 'data' folder."
	dumps="$dumps${dumps:+;}builder/gen/dump_$id.cpp"
done
say "Preparando las fuentes..." "Preparing the sources..."
VNBA_SPLIT=builder/gen VNBA_SPLIT_INPUTS="$dumps" ./VirtuaTennis-Recompiled || true
[ -f builder/gen/aot_gen_table.cpp ] || fail "No se han podido preparar las fuentes." "Could not prepare the sources."
rm -f builder/gen/dump_*.cpp

say "Compilando ($(nproc) núcleos, de 5 a 25 minutos)..." "Compiling ($(nproc) cores, 5 to 25 minutes)..."
make -C builder -f Makefile.linux -j"$(nproc)" --no-print-directory

mkdir -p data
printf '%s\n' "${games[@]}" > data/vt_games.txt
rm -rf builder/gen builder/obj	# generated sources contain code derived from the games
say "¡Listo! Ya puedes ejecutar ./VirtuaTennis-Recompiled" "Done! You can now run ./VirtuaTennis-Recompiled"
