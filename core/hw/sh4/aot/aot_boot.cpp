// BIOS-free boot for the Virtua Tennis recompilation (Virtua Tennis and Virtua Tennis 2, NAOMI
// cartridges). Derived from the Virtua NBA recompilation.
//
// The NAOMI BIOS only has to run once per game and region: when the game reaches its entry point
// (gamePC, read from the ROM header: 0x0C021000 for both games) the complete machine state is
// captured into a boot snapshot. Later launches restore that snapshot instead of running the
// BIOS, so naomi.zip is no longer needed.
//
// After the entry point the games never execute BIOS ROM code, but Virtua Tennis reads BIOS data
// well outside the few windows Virtua NBA needed (e.g. 0x127200 during the attract mode), so the
// whole 2 MB BIOS image is kept in the snapshot.
//
// Environment:
//   VNBA_BOOT_SNAPSHOT=path   snapshot file (default: <data dir>/<romset>_boot_<region>.state)
//   VNBA_BIOS_BOOT=1          ignore the snapshot and boot through the BIOS
#include "aot_boot.h"
#include "aot_game.h"
#include "types.h"
#include "serialize.h"
#include "stdclass.h"
#include <zlib.h>
#include <nowide/cstdio.hpp>
#include "hw/flashrom/nvmem.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/modules/mmu.h"
#include "hw/mem/addrspace.h"
#include "hw/pvr/ta_ctx.h"
#include "cfg/option.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <iterator>
#include <cmath>
#include <algorithm>

namespace {

constexpr u32 MAGIC = 0x54425456;	// "VTBT"
constexpr u32 FORMAT = 1;
struct Window { u32 start, size; };
constexpr Window BIOS_WINDOWS[] = {
	{ 0x000000, 0x200000 },
};
constexpr u32 windowBytes()
{
	u32 n = 0;
	for (const Window& w : BIOS_WINDOWS)
		n += w.size;
	return n;
}

struct Header {
	u32 magic;
	u32 format;
	u32 entry;
	u32 biosWindows;
	u32 biosWindowBytes;
	u32 stateSize;
	u32 compressedSize;
};

bool checked, available, captured, restored;
std::string path;
std::string forGame;

// config::Region for NAOMI: 0 Japan, 1 USA, 2 Export, 3 Korea
int regionIndex(int region)
{
	return region >= 0 && region <= 3 ? region : 1;
}

std::string snapshotPath(const std::string& game, int region)
{
	static const char * const names[] { "jp", "us", "ex", "kr" };
	return get_writable_data_path(game + "_boot_" + names[regionIndex(region)] + ".state");
}

bool validSnapshot(const std::string& file)
{
	FILE *f = nowide::fopen(file.c_str(), "rb");
	if (f == nullptr)
		return false;
	Header h{};
	bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == MAGIC && h.format == FORMAT
			&& h.biosWindows == std::size(BIOS_WINDOWS) && h.biosWindowBytes == windowBytes();
	fclose(f);
	return ok;
}

void init()
{
	if (checked)
		return;
	checked = true;
	forGame = aot_game_id();
	captured = restored = false;
	if (const char *p = getenv("VNBA_BOOT_SNAPSHOT"))
		path = p;
	else
		path = snapshotPath(forGame, config::Region);	// one snapshot per game and BIOS region
	available = false;
	if (getenv("VNBA_BIOS_BOOT") != nullptr)
		return;
	available = validSnapshot(path);
	INFO_LOG(BOOT, "Boot snapshot %s: %s", path.c_str(), available ? "found" : "not found");
}

// a different game was loaded since the last check (launcher): start over
void checkGame()
{
	if (checked && aot_game_id() != forGame)
		checked = false;
}

} // namespace

std::string aot_boot_snapshot_path(const std::string& game, int region)
{
	return snapshotPath(game, region);
}

// ---- Native widescreen ----
// Every 3D camera of both games is set up from a literal pool holding the far plane (3000, or 300
// for the player-select cameras), the near plane (0.3) and the aspect ratio 4/3, in various
// orders (e.g. the VT2 match camera at 0x0C026ED8, plus replay,
// attract, close-up and menu cameras). Replacing the aspect with 16/9 makes the game project and
// cull a Hor+ 16:9 view itself (net, crowd and players stay visible at the sides); the renderer
// then stretches the 4:3 frame to 16:9 and the 2D parts (HUD, menus) are squeezed back to 4:3.
// The program area is rescanned every few frames so that pools that are (re)written later are
// patched too.
//   VNBA_WS_MODE=stretch|off   (product: Settings -> Video -> Widescreen 16:9)
bool aot_ws_active;
static void wsPolyHook(Vertex *v, u32 count, u32 listType, PolyParam *pp);
extern void (*vnba_poly_hook)(Vertex *v, u32 count, u32 listType, PolyParam *pp);

static bool wsEnabled()
{
	if (const char *e = getenv("VNBA_WS_MODE"))
		return strcmp(e, "stretch") == 0;
#ifdef VNBA_PRODUCT
	static bool requested = config::Widescreen;	// the checkbox selects the native mode
	return requested;
#else
	return false;
#endif
}

bool vnba_native_widescreen()
{
	return wsEnabled();
}

void aot_ws_tick()
{
	static u32 n;
	if ((n++ & 0x3FFF) != 0)	// ~ every 2 frames
		return;
	constexpr u32 FAR_3000 = 0x453B8000, FAR_300 = 0x43960000;
	static const float WIDE = getenv("VNBA_WS_ASPECT") ? (float)atof(getenv("VNBA_WS_ASPECT")) : 16.f / 9.f;
	u32 wide;
	memcpy(&wide, &WIDE, 4);
	const AotGameInfo& g = aot_game_info();
	if (!g.valid)
		return;
	const u32 start = std::max(g.progStart, 0x0C000000u) + 8;
	const u32 end = std::min(g.progEnd, 0x0C000000u + RAM_SIZE);
	for (u32 a = start; a + 12 < end; a += 4)
	{
		const u32 *w = (const u32 *)&mem_b[(a - 8) & RAM_MASK];
		if ((w[2] & 0xFFFFFFF0) != 0x3FAAAAA0)	// ~4/3
			continue;
		bool camera = false;
		for (int k = -1; k <= 5 && !camera; k++)
			if (k != 2 && (w[k] == FAR_3000 || w[k] == FAR_300))
				camera = true;
		if (!camera)
			continue;
		addrspace::write32(a, wide);	// through the bus: write protection re-verifies the page
	}
}

extern u32 aot_hook_pc, aot_hook_pc2;
void aot_run_hook(u32 pc, Sh4Context *ctx)
{
}

static void applyGamePatches()
{
	extern float vnba_sprite_squeeze, vnba_sprite_keep_z;
	extern bool vnba_sprite_fill_edges, vnba_sprite_keep_fullscreen;
	aot_ws_active = wsEnabled();
	if (!aot_ws_active)
	{
		vnba_sprite_squeeze = 1.f;
		if (vnba_poly_hook == wsPolyHook)
			vnba_poly_hook = nullptr;
		return;
	}
	config::Widescreen.override(false);	// the game now renders the wide view itself
	config::ScreenStretching.override(133);	// 4:3 frame -> 16:9 display
	vnba_sprite_squeeze = getenv("VNBA_HUD_STRETCH") ? 1.f : 0.75f;
	vnba_sprite_fill_edges = getenv("VNBA_2D_NOFILL") == nullptr;
	vnba_sprite_keep_z = 0.f;
	vnba_sprite_keep_fullscreen = true;
	if (getenv("VNBA_HUD_STRETCH") == nullptr)
		vnba_poly_hook = wsPolyHook;
	aot_ws_tick();
	NOTICE_LOG(BOOT, "Native widescreen (16:9 cameras, 4:3 HUD)");
}

static bool writeSnapshot(const std::string& file)
{
	Serializer dry;
	dc_serialize(dry);
	std::vector<u8> state(dry.size());
	Serializer ser(state.data(), state.size());
	dc_serialize(ser);

	uLongf packedLen = compressBound(state.size());
	std::vector<u8> packed(packedLen);
	if (compress2(packed.data(), &packedLen, state.data(), state.size(), 6) != Z_OK)
		return false;
	std::vector<u8> window;
	for (const Window& w : BIOS_WINDOWS)
		window.insert(window.end(), nvmem::getBiosData() + w.start, nvmem::getBiosData() + w.start + w.size);
	// the BIOS image compresses well too
	uLongf packedWinLen = compressBound(window.size());
	std::vector<u8> packedWin(packedWinLen);
	if (compress2(packedWin.data(), &packedWinLen, window.data(), window.size(), 6) != Z_OK)
		return false;
	Header h{ MAGIC, FORMAT, aot_game_info().entry, (u32)std::size(BIOS_WINDOWS), windowBytes(), (u32)state.size(), (u32)packedLen };
	FILE *raw = nowide::fopen(file.c_str(), "wb");
	if (raw == nullptr)
	{
		WARN_LOG(BOOT, "Cannot write boot snapshot %s", file.c_str());
		return false;
	}
	const u32 winLen = (u32)packedWinLen;
	bool ok = fwrite(&h, sizeof(h), 1, raw) == 1
			&& fwrite(&winLen, sizeof(winLen), 1, raw) == 1
			&& fwrite(packedWin.data(), 1, winLen, raw) == winLen
			&& fwrite(packed.data(), 1, packedLen, raw) == packedLen;
	fclose(raw);
	if (!ok)
	{
		WARN_LOG(BOOT, "Error writing boot snapshot %s", file.c_str());
		remove(file.c_str());
		return false;
	}
	NOTICE_LOG(BOOT, "Boot snapshot saved to %s (%zu bytes of state, %lu compressed)", file.c_str(), state.size(), (unsigned long)packedLen);
	return true;
}

// Experiments: save a snapshot (same format) at an arbitrary point, e.g. in the middle of a match
void aot_boot_save_snapshot(const char *file)
{
	writeSnapshot(file);
}

bool aot_boot_snapshot_available()
{
	checkGame();
	init();
	return available;
}

// No snapshot for the selected region and no BIOS to create it: boot from another region's
// snapshot of the same game instead. Returns false if there is none at all.
bool aot_boot_use_fallback()
{
	checkGame();
	init();
	if (available || getenv("VNBA_BOOT_SNAPSHOT") != nullptr)
		return available;
	for (int r : { 1, 2, 0, 3 })
	{
		std::string c = snapshotPath(forGame, r);
		if (validSnapshot(c))
		{
			WARN_LOG(BOOT, "No boot snapshot for region %d, using %s", (int)config::Region, c.c_str());
			path = c;
			available = true;
			return true;
		}
	}
	return false;
}

// After a reset (or when another game was loaded) the machine restarts from the BIOS entry
// point: boot from the snapshot again.
void aot_boot_rearm()
{
	checkGame();
	aot_game_invalidate();
	if (!checked)
		return;
	restored = false;
	if (!available && getenv("VNBA_BIOS_BOOT") == nullptr)
		available = validSnapshot(path);	// captured during this session
}

// Called before every block dispatch until the snapshot is captured or restored (hot path:
// only cheap checks here). Returns true if the machine state was replaced.
bool aot_boot_hook(u32 pc)
{
	init();
	if (available && !restored)
	{
		restored = true;
		FILE *raw = nowide::fopen(path.c_str(), "rb");
		Header h{};
		u32 winLen = 0;
		if (raw == nullptr || fread(&h, sizeof(h), 1, raw) != 1 || fread(&winLen, sizeof(winLen), 1, raw) != 1)
		{
			if (raw) fclose(raw);
			throw FlycastException("Cannot read boot snapshot " + path);
		}
		if (h.biosWindows != std::size(BIOS_WINDOWS) || h.biosWindowBytes != windowBytes())
		{
			fclose(raw);
			throw FlycastException("Incompatible boot snapshot " + path);
		}
		std::vector<u8> packedWin(winLen);
		std::vector<u8> window(h.biosWindowBytes);
		std::vector<u8> packed(h.compressedSize);
		std::vector<u8> state(h.stateSize);
		bool ok = fread(packedWin.data(), 1, packedWin.size(), raw) == packedWin.size()
				&& fread(packed.data(), 1, packed.size(), raw) == packed.size();
		fclose(raw);
		uLongf winOut = window.size();
		uLongf outLen = state.size();
		if (!ok || uncompress(window.data(), &winOut, packedWin.data(), packedWin.size()) != Z_OK || winOut != window.size()
				|| uncompress(state.data(), &outLen, packed.data(), packed.size()) != Z_OK || outLen != state.size())
			throw FlycastException("Corrupted boot snapshot " + path);

		Deserializer deser(state.data(), state.size());
		dc_deserialize(deser);
		mmu_set_state();
		u8 *bios = nvmem::getBiosData();
		u32 off = 0;
		for (const Window& w : BIOS_WINDOWS)
		{
			memcpy(bios + w.start, window.data() + off, w.size);
			off += w.size;
		}
		NOTICE_LOG(BOOT, "Booted from snapshot (BIOS-free), pc %08x", Sh4cntx.pc);
		applyGamePatches();
		return true;
	}
	if (!available && !captured && aot_game_info().valid && (pc & 0x1FFFFFFF) == aot_game_info().entry)
	{
		captured = true;
		struct PatchAfterCapture { ~PatchAfterCapture() { applyGamePatches(); } } patchAfter;	// keep the snapshot pristine
		writeSnapshot(path);
		return true;	// at the game entry point: same moment as a snapshot restore
	}
	return false;
}

bool aot_boot_pending()
{
	init();
	return available ? !restored : !captured;
}

bool aot_boot_bios_free()
{
	return restored;
}

bool aot_bios_window(u32 addr)
{
	for (const Window& w : BIOS_WINDOWS)
		if (addr >= w.start && addr < w.start + w.size)
			return true;
	return false;
}

// ---- Diagnostics: VNBA_POLY_LOG=file logs every polygon strip and sprite (screen space) ----
static FILE *polyLog;
static void polyLogHook(Vertex *v, u32 count, u32 listType, PolyParam *pp)
{
	static const u64 from = getenv("VNBA_POLY_LOG_FROM") ? atoll(getenv("VNBA_POLY_LOG_FROM")) * (SH4_MAIN_CLOCK / 60) : 0;
	static const u64 to = getenv("VNBA_POLY_LOG_TO") ? atoll(getenv("VNBA_POLY_LOG_TO")) * (SH4_MAIN_CLOCK / 60) : ~0ull;
	const u64 now = sh4_sched_now64();
	if (now < from || now > to)
		return;
	float x0 = v[0].x, x1 = v[0].x, y0 = v[0].y, y1 = v[0].y, z0 = v[0].z, z1 = v[0].z;
	for (u32 i = 1; i < count; i++)
	{
		x0 = std::min(x0, v[i].x); x1 = std::max(x1, v[i].x);
		y0 = std::min(y0, v[i].y); y1 = std::max(y1, v[i].y);
		z0 = std::min(z0, v[i].z); z1 = std::max(z1, v[i].z);
	}
	fprintf(polyLog, "L%u n%u x %.1f %.1f y %.1f %.1f z %g %g pcw %08x isp %08x tsp %08x tcw %08x t %llu\n", listType, count, x0, x1, y0, y1, z0, z1,
			pp->pcw.full, pp->isp.full, pp->tsp.full, pp->tcw.full, (unsigned long long)(now / (SH4_MAIN_CLOCK / 60)));
}
static struct PolyLogInit {
	PolyLogInit() {
		if (const char *p = getenv("VNBA_POLY_LOG"))
		{
			polyLog = fopen(p, "w");
			vnba_poly_hook = polyLogHook;
			extern void (*vnba_sprite_hook)(Vertex *v, u32 count, u32 listType, PolyParam *pp);
			vnba_sprite_hook = polyLogHook;
		}
	}
} polyLogInit;

// Widescreen: the HUD and the menus of both games also use ordinary polygons (score panels,
// speed meter, player-select frames) drawn at a constant depth. Squeeze them like the sprites so
// the whole 2D layer keeps its 4:3 proportions.
static void wsPolyHook(Vertex *v, u32 count, u32 listType, PolyParam *pp)
{
	extern bool vnba_stretch_frame;
	if (vnba_stretch_frame)
		return;
	const float z = v[0].z;
	float minx = v[0].x, maxx = v[0].x, miny = v[0].y, maxy = v[0].y;
	for (u32 i = 1; i < count; i++)
	{
		if (std::fabs(v[i].z - z) > z * 1e-4f)
			return;
		minx = std::min(minx, v[i].x);
		maxx = std::max(maxx, v[i].x);
		miny = std::min(miny, v[i].y);
		maxy = std::max(maxy, v[i].y);
	}
	extern float vnba_sprite_squeeze;
	// Full-screen layers drawn through the 3D camera (VT1 attract blur / grey fade: flat quads
	// in front of the camera sized for the 4:3 view): with the 16:9 camera they cover only the
	// middle 3/4 of the frame. Widen them back to the full frame.
	if (miny <= 1.f && maxy >= 479.f && std::fabs((minx + maxx) * 0.5f - 320.f) < 2.f
			&& minx > 1.f && maxx - minx >= 640.f * vnba_sprite_squeeze - 8.f)
	{
		for (u32 i = 0; i < count; i++)
			v[i].x = 320.f + (v[i].x - 320.f) / vnba_sprite_squeeze;
		return;
	}
	// Flat polygons are 2D: the HUD (constant depth >= 1) and the menus (player select: frames,
	// boxes and panels at ~0.0025). The 3D scene never produces polygons of constant depth.
	if (minx <= 1.f && maxx >= 639.f)
		return;	// full-screen 2D layer (fade)
	for (u32 i = 0; i < count; i++)
		v[i].x = 320.f + (v[i].x - 320.f) * vnba_sprite_squeeze;
}
