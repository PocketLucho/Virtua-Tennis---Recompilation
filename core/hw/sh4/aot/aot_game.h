#pragma once
#include "types.h"
#include <string>

// Per-game parameters for the AOT recompilation runtime. Several NAOMI games can share one
// executable (their blocks coexist in the AOT table, told apart by the hash of their code);
// everything that depends on the running game is read from the cartridge header.
struct AotGameInfo
{
	bool valid = false;
	u32 entry = 0;			// gamePC from the ROM header (physical)
	u32 progStart = 0;		// lowest RAM address loaded from the ROM (physical)
	u32 progEnd = 0;		// end of the loaded program (physical, exclusive)
	char title[33] {};
};

// Romset name of the loaded content ("vtennis", "vtenis2c")
std::string aot_game_id();
// Header information of the running game (valid once the cartridge is loaded)
const AotGameInfo& aot_game_info();
// forget the cached header (new game loaded / reset)
void aot_game_invalidate();
static inline bool aot_game_in_program(u32 a)
{
	const AotGameInfo& g = aot_game_info();
	u32 p = a & 0x1FFFFFFF;
	return p >= g.progStart && p < g.progEnd && (a & 1) == 0;
}
