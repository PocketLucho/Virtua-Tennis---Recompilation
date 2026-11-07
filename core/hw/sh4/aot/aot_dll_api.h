// Interface between the engine (VirtuaTennis-Recompiled.exe, distributable, contains no game code)
// and the game module (vt_game.dll / libvt_game.so) that every user builds from their own
// ROM with the builder. The module only contains the recompiled SH-4 blocks.
#pragma once
#include "aot_runtime.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_rom.h"
#include <cstddef>

#define VNBA_GAME_API_VERSION 1

struct AotHostApi
{
	u32 version;
	u32 contextSize;	// sizeof(Sh4Context): both sides must be built from the same headers
	// data
	u8 *ram;
	u32 ramMask;
	OpCallFP **opPtr;	// 0x10000 entries
	const f32_x2 *sinTable;	// 0x10000 entries
	// functions
	u8 (DYNACALL *read8)(u32);
	u16 (DYNACALL *read16)(u32);
	u32 (DYNACALL *read32)(u32);
	u64 (DYNACALL *read64)(u32);
	void (DYNACALL *write8)(u32, u8);
	void (DYNACALL *write16)(u32, u16);
	void (DYNACALL *write32)(u32, u32);
	void (DYNACALL *write64)(u32, u64);
	void (*doException)(u32, Sh4ExceptionCode);
	bool (*updateSR)();
	void (DYNACALL *updateFPSCR)(Sh4Context *);
	void (*updateIntc)();
	void (*biosRead)(u32);
	u64 (*replayRead)(u32, u32);
	void (*replayWrite)(u32, u32, u64);
};

// Exported by the game module. Returns the block table (sorted by vaddr) or nullptr if the
// host is incompatible.
typedef const AotBlockEntry *(*VnbaGameInitFn)(const AotHostApi *host, size_t *count);
#define VNBA_GAME_INIT_SYMBOL "vnba_game_init"

// romsets the game module was built for (written by the builder into the data folder)
#define VNBA_GAME_LIST "vt_games.txt"

#ifdef _WIN32
#define VNBA_GAME_MODULE "vt_game.dll"
#else
#define VNBA_GAME_MODULE "libvt_game.so"
#endif
