#!/bin/bash
cd "${VNBA_WORK:?set VNBA_WORK to your work folder (roms/, sys/, boot.state, host/lr_headless)}"
for s in 11 22 33; do
  VNBA_AOT_DUMP=$PWD/cov/miss_$s.cpp VNBA_BOOT_SNAPSHOT=$PWD/boot.state timeout 900 ./host/lr_headless ${CORE_AOT:?path to flycast_libretro.so built with the blocks} roms/${ROM:-vtenis2c}.zip $PWD/sys_nobios 9000 cov 3000 inputR$s.txt > cov/log_$s.txt 2>&1
  mkdir -p cov/shots_$s; mv cov/frame_*.ppm cov/shots_$s/ 2>/dev/null
done
echo ALLDONE > cov/done.txt
