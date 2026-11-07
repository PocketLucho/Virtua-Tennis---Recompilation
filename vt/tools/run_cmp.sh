#!/bin/bash
# deterministic guest-time comparison: reference (strict blocks) vs AOT
cd "${VNBA_WORK:?set VNBA_WORK to your work folder (roms/, sys/, boot.state, host/lr_headless)}"
export VNBA_FIXED_RTC=1
N=${1:-12000}
VNBA_AOT_DUMP=/dev/null VNBA_SLICE_DIGEST=$PWD/D1/digest.txt timeout 1500 ./host/lr_headless ${CORE_REF:?path to the reference flycast_libretro.so} roms/${ROM:-vtenis2c}.zip $PWD/sys $N D1 0 input2.txt > D1/log.txt 2>&1
echo DONE >> D1/log.txt
VNBA_SLICE_DIGEST=$PWD/D2/digest.txt timeout 1500 ./host/lr_headless ${CORE_AOT:?path to flycast_libretro.so built with the blocks} roms/${ROM:-vtenis2c}.zip $PWD/sys $N D2 0 input2.txt > D2/log.txt 2>&1
echo DONE >> D2/log.txt
