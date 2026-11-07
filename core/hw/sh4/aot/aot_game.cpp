#include "aot_game.h"
#include "emulator.h"
#include "hw/naomi/naomi_cart.h"
#include <cstring>
#include <algorithm>

std::string aot_game_id()
{
	std::string p = settings.content.path;
	size_t slash = p.find_last_of("/\\");
	if (slash != std::string::npos)
		p = p.substr(slash + 1);
	size_t dot = p.find_last_of('.');
	if (dot != std::string::npos)
		p = p.substr(0, dot);
	std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return p.empty() ? std::string("game") : p;
}

static bool gameInfoStale = true;
void aot_game_invalidate()
{
	gameInfoStale = true;
}

const AotGameInfo& aot_game_info()
{
	static AotGameInfo info;
	static Cartridge *cachedCart;
	if (info.valid && cachedCart == CurrentCartridge && !gameInfoStale)
		return info;
	info = AotGameInfo{};
	cachedCart = CurrentCartridge;
	gameInfoStale = false;
	RomBootID id;
	if (CurrentCartridge == nullptr || !CurrentCartridge->GetBootId(&id))
		return info;
	info.entry = id.gamePC & 0x1FFFFFFF;
	u32 lo = 0xFFFFFFFF, hi = 0;
	for (int i = 0; i < 8; i++)
	{
		if (id.gameLoad[i][0] == 0xFFFFFFFF)
			break;
		u32 start = id.gameLoad[i][1] & 0x1FFFFFFF;
		lo = std::min(lo, start);
		hi = std::max(hi, start + id.gameLoad[i][2]);
	}
	if (lo >= hi)
	{
		// Not described by the header: assume the usual NAOMI program area
		lo = 0x0C020000;
		hi = 0x0C420000;
	}
	info.progStart = lo;
	info.progEnd = hi;
	memcpy(info.title, id.gameTitle[0], 32);
	for (int i = 31; i >= 0 && (info.title[i] == ' ' || info.title[i] == '-' || info.title[i] == 0); i--)
		info.title[i] = 0;
	info.valid = info.entry != 0;
	INFO_LOG(BOOT, "AOT game '%s': entry %08x, program %08x-%08x", info.title, info.entry, info.progStart, info.progEnd);
	return info;
}
