# Technical overview

## Games

| Romset | Game | Cartridge header (game ID) | Entry point | Program |
|---|---|---|---|---|
| `vtennis` | Virtua Tennis / Power Smash (1999) | ` POWER SMASH --------------` | 0x0C021000 | 4 MB from 0x0C020000 |
| `vtenis2c` | Virtua Tennis 2 / Power Smash 2 (2001) | ` POWER SMASH 2 -----------` | 0x0C021000 | 4 MB from 0x0C020000 |

Everything that depends on the running game (entry point, program range, snapshot names, patches) is read from
the cartridge header (`core/hw/sh4/aot/aot_game.cpp`). The launcher (`core/vnba_product.cpp`) lists the games,
checks which romsets are present and which ones the builder generated code for (`data/vt_games.txt`).

## Pipeline

```
vtennis.zip / vtenis2c.zip ──► engine boots the game (BIOS once, then boot snapshot)
                 │  at the game entry point 0x0C021000 the program is in RAM
                 ▼
          static decoder (core/hw/sh4/aot/aot_static.cpp)
            seeds: vt/builder/seeds_<romset>.txt + every code pointer found in the program image
            + function prologues; follows branches / fall-through / call returns, SZ=0 and SZ=1 FPU variants
                 │  each block → Flycast SHIL IR → C++ (core/hw/sh4/dyna/aot_emit.cpp)
                 ▼  one dump per game (VNBA_AOT_DUMP)
          aot_split_dump(): both dumps → 48 translation units + one block table
                 │  clang (llvm-mingw) / g++
                 ▼
          vt_game.dll / libvt_game.so  ◄── loaded by the engine (core/hw/sh4/aot/aot_module.cpp)
```

`vt/builder/build_game.ps1` (Windows) and `build_game.sh` (Linux) drive it: for each romset found they run the
engine with `VNBA_AOT=1 VNBA_START_GAME=<romset> VNBA_AOT_DUMP=... VNBA_AOT_STATIC=seeds_<romset>.txt
VNBA_AOT_STATIC_PTR=1 VNBA_AOT_STATIC_SZ1=1 VNBA_AOT_STATIC_FUNCS=1`; the engine decodes the program at the entry
point, writes the dump and exits. `VNBA_SPLIT=<dir> VNBA_SPLIT_INPUTS=<dump;dump>` then splits the dumps and the
makefile compiles the module. About 170,000 blocks are generated for the two games; the few thousand blocks that
are not precompiled (BIOS code before the entry point, code built at run time) run in the interpreter.

## Engine ↔ game module

The generated code only needs a handful of engine services (memory access slow paths, exceptions, SR/FPSCR
updates, interrupt check, opcode table for interpreter fallbacks, FSCA sine table). The module receives
them in an `AotHostApi` table (`core/hw/sh4/aot/aot_dll_api.h`); `aot_dll_shim.cpp` defines the matching
symbols inside the DLL, so the generated sources are identical for the linked-in and the DLL builds. The API
carries a version and `sizeof(Sh4Context)`; an incompatible module is rejected.

Block identity is `(vaddr, fpscr & 0x00180003, FNV-1a hash of the guest bytes)`, so both games share one table.
RAM pages holding generated code are write-protected and re-validated on change.

## Executor (core/hw/sh4/aot/aot_executor.cpp)

* Page directory dispatch `pageDir[pc >> 12][(pc & 0xfff) >> 1]` → slot with block versions.
* Cycle accounting identical to Flycast's dynarec (cycles charged up front, same block boundaries).
* Anything without a precompiled block runs through a cycle-exact interpreter fallback.

## Boot snapshot (core/hw/sh4/aot/aot_boot.cpp)

The first start of a game runs the NAOMI BIOS; at the game entry point the machine state plus the BIOS window
the game reads (0x000000-0x200000, zlib-compressed) are saved to `data/<romset>_boot_<region>.state`
(jp/us/ex/kr). Later starts restore it and never need the BIOS. A reset re-arms the restore.

## Widescreen (native 16:9)

* Every 3D camera of both games is set up from a literal pool holding the far plane (3000, or 300 for the
  player-select cameras), the near plane and the aspect ratio 4/3. Every 4/3 constant found within 12 bytes of
  such a far plane is replaced with 16/9, so the game projects and culls a real 16:9 view. The program is
  rescanned periodically because some pools are written later.
* The frontend stretches the 640x480 frame by 133 %. `core/hw/pvr/ta_vtx.cpp` squeezes the 2D layer back by 0.75
  around x = 320: sprites, and flat polygons (constant depth: score panels, menu frames) through the polygon hook.
* Full-screen 2D pictures are extended with their edge texel column; full-screen layers positioned through the
  (now 16:9) camera, such as the attract-mode blur, are widened back to the whole frame.
* The title screen is detected by its background tiles (sprites at depth 2.2222, opaque or translucent while
  fading in) and drawn unsqueezed (stretched). The decision is carried to the next frame because the tiles are
  sent after the logo; an opaque full-screen layer in front of the tiles (the parental warning) cancels it.

## Cheats (core/cheats.cpp)

The game ID starts with ` POWER SMASH`. Score structure, points index 0..3 (0/15/30/40):

| | Player points | Rival points |
|---|---|---|
| Virtua Tennis | 0x0C305E1C | 0x0C305E20 |
| Virtua Tennis 2 | 0x0C23D258 | 0x0C23D25C |

In Virtua Tennis 2 the games are at 0x0C23D250 (player) / 0x0C23D254 (rival) and the deuce/advantage state at
0x0C23D260. *Rival can't score* writes 0 to the rival points every frame; *Start every game at 40* writes 3 to the
player points.

## Useful environment variables

| Variable | Effect |
|---|---|
| `VNBA_AOT=0/1` | force dynarec / AOT executor |
| `VNBA_GAME_MODULE_PATH` | path of the game module |
| `VNBA_START_GAME=<romset>` | skip the launcher |
| `VNBA_BOOT_SNAPSHOT`, `VNBA_BIOS_BOOT=1` | snapshot file / ignore it |
| `VNBA_WS_MODE=off|stretch` | widescreen override |
| `VNBA_FIXED_RTC=1` | deterministic runs |
| `VNBA_CHEATS=1` | enable the built-in cheats (testing) |
| `VNBA_SAVE_AT=<frame> VNBA_SAVE_FILE=<file>` | save a state at a frame |
| `VNBA_POKE=addr=u:value,...`, `VNBA_POLY_LOG=<file>` | analysis tools |

## Test frontend

`vt/host/lr_headless.cpp` runs the libretro core without a window (EGL), saves frames, replays input scripts
(`<frame> <port> <button> <0|1>`) and can dump RAM (`LR_RAMDUMP_FRAME` + `LR_RAMDUMP_FILE`, or
`LR_RAMDUMP_EVERY=<n>`).
