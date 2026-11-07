VIRTUA TENNIS - RECOMP  (Made by Pocket_lucho with Claude)
=========================================================

Static recompilation of Virtua Tennis (1999) and Virtua Tennis 2 (2001), Sega NAOMI.
If you want to support me: https://buymeacoffee.com/pocket_lucho

SETUP
1. Copy your romsets to the "roms" folder: vtennis.zip (Virtua Tennis) and/or
   vtenis2c.zip (Virtua Tennis 2).
2. Copy the NAOMI BIOS naomi.zip to the "data" folder (only needed the first time each
   game and region starts).
3. Run BUILD_GAME.bat ONCE. It downloads the free llvm-mingw compiler
   (180 MB, first time only), generates the game code from YOUR ROMs and compiles it
   into vt_game.dll (5 to 20 minutes).
4. Run VirtuaTennis-Recompiled.exe, choose a game and press PLAY.
   If you add the other game later, run BUILD_GAME.bat again.

This package contains nothing from the games: no ROM, BIOS, graphics, sound or code.
vt_game.dll and data\*_boot_*.state are generated from your files: do not share them.

OPTIONS (Tab key or the menu)
- Video: "Widescreen 16:9" (real 16:9 camera, 4:3 scoreboard and menus),
  "Antialiasing (FXAA)", Internal Resolution (supersampling).
- General: Region (Japan, USA, Export, Korea). naomi.zip is needed the first time a region
  is used.
- Menu: Reset, Cheats ("Rival can't score" makes it easy to clear the Arcade mode on
  Normal without losing a game and without continues, which unlocks Master / King;
  "Start every game at 40").
- Network: online play with GGPO (experimental). Both players need the same version, the
  same romset and the same widescreen setting.

TROUBLESHOOTING
- "Game code module not found" or "Run the builder": run BUILD_GAME.bat.
- After updating to a new version: run BUILD_GAME.bat again.
- Boot problems: delete data\*_boot_*.state (recreated with naomi.zip).

GPL v2 license (based on Flycast). Source code:
https://github.com/PocketLucho/Virtua-Tennis---Recompilation
