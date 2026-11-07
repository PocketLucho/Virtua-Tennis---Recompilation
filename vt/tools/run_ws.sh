#!/bin/bash
# usage: run_ws.sh outdir "POKES"
cd "${VNBA_WORK:?set VNBA_WORK to your work folder (roms/, sys/, boot.state, host/lr_headless)}"; rm -rf $1; mkdir -p $1
VNBA_FIXED_RTC=1 VNBA_WS_MODE=${WSMODE:-off} LR_OPTS="${WSOPT:-reicast_widescreen_hack=enabled}" VNBA_POKE="$2" VNBA_BOOT_SNAPSHOT=$PWD/boot.state timeout 590 ./host/lr_headless ${CORE_AOT:?path to flycast_libretro.so built with the blocks} roms/${ROM:-vtenis2c}.zip $PWD/sys_nobios ${FRAMES:-3901} $1 ${STEP:-300} input1.txt > $1/log.txt 2>&1
[ -z "$KEEP" ] && rm -f $1/frame_0[0-2]*.ppm
