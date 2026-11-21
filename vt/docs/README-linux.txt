VIRTUA TENNIS - RECOMP for Linux x86_64 and SteamOS  (Made by Pocket_lucho with Claude)
=======================================================================================

Static recompilation of Virtua Tennis (1999) and Virtua Tennis 2 (2001), Sega NAOMI.
If you want to support me: https://buymeacoffee.com/pocket_lucho

REQUIREMENTS: nothing to install. Works on SteamOS (Steam Deck, Legion Go S...), Ubuntu,
Fedora, Arch, etc. If the system has no C++ compiler, build_game.sh downloads a portable
one (Zig, ~45 MB) the first time, inside this folder. No sudo needed.
An internet connection is only needed the first time if you do not have g++.

SETUP
1. Copy your romsets to the "roms" folder: vtennis.zip (Virtua Tennis) and/or
   vtenis2c.zip (Virtua Tennis 2).
2. Copy the NAOMI BIOS naomi.zip to the "data" folder (only needed the first time each
   game and region starts).
3. Open a terminal in this folder and run ONCE:   ./build_game.sh
   It generates the game code from YOUR ROMs and compiles it into libvt_game.so
   (5 to 25 minutes).
4. To play run:   ./VirtuaTennis.sh   , choose a game and press PLAY.
   (if it fails, the reason is shown in a window and saved to vt.log)
   If you add the other game later, run ./build_game.sh again.

STEAMOS (Steam Deck / Legion Go S)
1. Switch to Desktop Mode (Steam button > Power > Switch to Desktop).
2. Extract the .tar.gz (for example to ~/Games/VirtuaTennis-Recomp) and copy the ROMs and BIOS.
3. In the file manager (Dolphin), inside the folder: right click > "Open Terminal Here"
   and type:
       chmod +x build_game.sh VirtuaTennis.sh VirtuaTennis-Recompiled
       ./build_game.sh
4. Test the game with ./VirtuaTennis.sh
5. To play in Game Mode: open Steam on the desktop > "Add a Game" > "Add a Non-Steam Game"
   > Browse > pick VirtuaTennis.sh (set the filter to "All Files"). Go back to Game Mode and it
   will be in your library. The gamepad works directly; the settings menu opens with the
   Tab key or the button mapped in the controls options.

This package contains nothing from the games: no ROM, BIOS, graphics, sound or code.
libvt_game.so and data/*_boot_*.state are generated from your files: do not share them.

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
- "Game code module not found" or "Run the builder": run ./build_game.sh.
- After updating to a new version: run ./build_game.sh again.
- Boot problems: delete data/*_boot_*.state (recreated with naomi.zip).

GPL v2 license (based on Flycast). Source code:
https://github.com/PocketLucho/Virtua-Tennis---Recompilation
