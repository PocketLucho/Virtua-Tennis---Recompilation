// SH-4 executor that runs ahead-of-time recompiled blocks (generated C++),
// falling back to the interpreter one instruction at a time for any PC that
// has no precompiled block. No JIT is involved.
//
// Environment:
//   VNBA_AOT=1            select this executor
//   VNBA_AOT_MISSES=path  write the set of PCs that needed the interpreter
//   VNBA_AOT_STATS=1      print block/fallback statistics on exit
#include "hw/pvr/Renderer_if.h"
#include "hw/sh4/aot/aot_cputimer.h"
#include "aot_runtime.h"
#include "aot_boot.h"
#include "aot_game.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_mem.h"
#include "debug/gdb_server.h"
#include <unordered_map>
#include <memory>
#include <deque>
#include <algorithm>
#include <map>
#include <chrono>
#include <x86intrin.h>
#include <unordered_set>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/dyna/blockmanager.h"

int UpdateSystem_INTC();
void aot_dump_block(RuntimeBlockInfo *block);
void bm_LockPage(u32 addr, u32 size);
void bm_UnlockPage(u32 addr, u32 size);
extern bool (*aot_ram_write_hook)(u32 addr);

namespace {

constexpr u32 FPU_MASK = 0x00180003;

// All precompiled versions (any fpu config) of the blocks starting at one guest address.
struct Slot {
	std::vector<const AotBlockEntry *> versions;
	const AotBlockEntry *cur = nullptr;	// version matching guest memory, valid while gen matches
	u32 gen = 0;
};

// RAM page tracking (4 KB pages): a generation counter that changes whenever the page is
// written after a block in it was verified. Pages written too often stay unlocked and
// their blocks are hash-checked on every execution (like the dynarec's block checks).
constexpr u32 AOT_PAGE_SHIFT = 12;
constexpr u32 MAX_PAGE_FAULTS = 32;
std::vector<u32> pageGen;
std::vector<u16> pageFaults;
std::vector<bool> pageLocked;
u32 ramMask;
bool trackPages;

void bmLock(u32 addr);
void bmUnlock(u32 addr);

u32 fnv1a(u32 addr, u32 size)
{
	u32 h = 2166136261u;
	for (u32 i = 0; i < size; i += 2)
	{
		u16 op = IReadMem16(addr + i);
		h = (h ^ (op & 0xff)) * 16777619u;
		h = (h ^ (op >> 8)) * 16777619u;
	}
	return h;
}

inline u64 key(u32 vaddr, u32 fpu) { return ((u64)fpu << 32) | vaddr; }

// ---- lockstep verification journal ----
struct Access {
	u32 addr;
	u32 size;	// 1,2,4,8 or 32 for a store-queue flush
	u64 value;
	bool write;
	bool optional;	// read from the block's own code pages: may have been constant-folded
};
std::vector<Access> journal;
u32 codePageLo, codePageHi;
inline bool inCodePages(u32 a) { return (a >> 12) >= codePageLo && (a >> 12) <= codePageHi; }
size_t journalPos;
bool replayError;
std::string replayErrorMsg;
bool intcPending;

ReadMem8Func realRead8; ReadMem16Func realRead16; ReadMem32Func realRead32; ReadMem64Func realRead64;
WriteMem8Func realWrite8; WriteMem16Func realWrite16; WriteMem32Func realWrite32; WriteMem64Func realWrite64;
SQWriteFunc *realSqWrite;

u8 DYNACALL recRead8(u32 a) { u8 v = realRead8(a); journal.push_back({a, 1, v, false, 1 <= 4 && inCodePages(a)}); return v; }
u16 DYNACALL recRead16(u32 a) { u16 v = realRead16(a); journal.push_back({a, 2, v, false, 2 <= 4 && inCodePages(a)}); return v; }
u32 DYNACALL recRead32(u32 a) { u32 v = realRead32(a); journal.push_back({a, 4, v, false, 4 <= 4 && inCodePages(a)}); return v; }
u64 DYNACALL recRead64(u32 a) { u64 v = realRead64(a); journal.push_back({a, 8, v, false, 8 <= 4 && inCodePages(a)}); return v; }
void DYNACALL recWrite8(u32 a, u8 v) { journal.push_back({a, 1, v, true, false}); realWrite8(a, v); }
void DYNACALL recWrite16(u32 a, u16 v) { journal.push_back({a, 2, v, true, false}); realWrite16(a, v); }
void DYNACALL recWrite32(u32 a, u32 v) { journal.push_back({a, 4, v, true, false}); realWrite32(a, v); }
void DYNACALL recWrite64(u32 a, u64 v) { journal.push_back({a, 8, v, true, false}); realWrite64(a, v); }
void DYNACALL recSqWrite(u32 a, Sh4Context *c) { journal.push_back({a, 32, 0, true, false}); realSqWrite(a, c); }

void replayFail(const char *what, u32 addr, u32 size, u64 value)
{
	if (replayError)
		return;
	replayError = true;
	char buf[256];
	if (journalPos < journal.size())
	{
		const Access& e = journal[journalPos];
		snprintf(buf, sizeof(buf), "%s %08x/%u=%llx, interpreter did %s %08x/%u=%llx (#%zu)", what, addr, size,
				(unsigned long long)value, e.write ? "write" : "read", e.addr, e.size, (unsigned long long)e.value, journalPos);
	}
	else
		snprintf(buf, sizeof(buf), "%s %08x/%u=%llx beyond interpreter journal (%zu)", what, addr, size, (unsigned long long)value, journal.size());
	replayErrorMsg = buf;
}

u8 DYNACALL repRead8(u32 a) { return (u8)aot_replay_read(a, 1); }
u16 DYNACALL repRead16(u32 a) { return (u16)aot_replay_read(a, 2); }
u32 DYNACALL repRead32(u32 a) { return (u32)aot_replay_read(a, 4); }
u64 DYNACALL repRead64(u32 a) { return aot_replay_read(a, 8); }
void DYNACALL repWrite8(u32 a, u8 v) { aot_replay_write(a, 1, v); }
void DYNACALL repWrite16(u32 a, u16 v) { aot_replay_write(a, 2, v); }
void DYNACALL repWrite32(u32 a, u32 v) { aot_replay_write(a, 4, v); }
void DYNACALL repWrite64(u32 a, u64 v) { aot_replay_write(a, 8, v); }
void DYNACALL repSqWrite(u32 a, Sh4Context *) { aot_replay_write(a, 32, 0); }

void setMemHandlers(int mode)
{
	if (mode == 0) {
		ReadMem8 = realRead8; ReadMem16 = realRead16; ReadMem32 = realRead32; ReadMem64 = realRead64;
		WriteMem8 = realWrite8; WriteMem16 = realWrite16; WriteMem32 = realWrite32; WriteMem64 = realWrite64;
	} else if (mode == 1) {
		ReadMem8 = recRead8; ReadMem16 = recRead16; ReadMem32 = recRead32; ReadMem64 = recRead64;
		WriteMem8 = recWrite8; WriteMem16 = recWrite16; WriteMem32 = recWrite32; WriteMem64 = recWrite64;
	} else {
		ReadMem8 = repRead8; ReadMem16 = repRead16; ReadMem32 = repRead32; ReadMem64 = repRead64;
		WriteMem8 = repWrite8; WriteMem16 = repWrite16; WriteMem32 = repWrite32; WriteMem64 = repWrite64;
	}
}

bool aotRamWrite(u32 addr)
{
	u32 page = (addr & ramMask) >> AOT_PAGE_SHIFT;
	if (page >= pageLocked.size() || !pageLocked[page])
		return false;
	pageLocked[page] = false;
	pageGen[page]++;
	if (getenv("VNBA_WATCH_OVERLAY") != nullptr && page >= 0x20 && page < 0x60)
	{
		// keep watching the overlay area: re-lock right after this write completes is not possible here,
		// so record and let blockGen re-lock on next verification
	}
	static FILE *codeWriteLog = getenv("VNBA_CODE_WRITE_LOG") ? fopen(getenv("VNBA_CODE_WRITE_LOG"), "w") : nullptr;
	if (codeWriteLog != nullptr)
		fprintf(codeWriteLog, "write %08x pc %08x pr %08x r4 %08x r5 %08x r6 %08x\n", 0x0C000000 | (addr & ramMask), Sh4cntx.pc, Sh4cntx.pr,
				Sh4cntx.r[4], Sh4cntx.r[5], Sh4cntx.r[6]);
	if (pageFaults[page] < 0xffff)
		pageFaults[page]++;
	bm_UnlockPage(page << AOT_PAGE_SHIFT, 1 << AOT_PAGE_SHIFT);
	return true;
}

} // namespace

bool aot_verify_replay;
u32 aot_hook_pc = 0xFFFFFFFF, aot_hook_pc2 = 0xFFFFFFFF;
void aot_run_hook(u32 pc, Sh4Context *ctx);
u32 aot_cur_block;
bool aot_bios_read_watch;
static std::map<u32, std::pair<u32, u32>> biosByPc;	// pc -> [lo, hi]
static u32 biosReads;
static void biosReport()
{
	for (auto& [pc, r] : biosByPc)
		fprintf(stderr, "BIOSWATCH: pc %08x read BIOS %08x-%08x\n", pc, r.first, r.second);
	fprintf(stderr, "BIOSWATCH: %u reads\n", biosReads);
}
void aot_bios_read(u32 a)
{
	u32 p = a & 0x1FFFFFFF;
	if (p >= 0x00200000)
		return;
	if (!aot_bios_window(p))
	{
		static int warned;
		if (warned++ < 10)
			WARN_LOG(BOOT, "VNBA: read of BIOS ROM %08x outside the boot snapshot windows (pc %08x)", p, Sh4cntx.pc);
	}
	if (biosReads++ == 0)
		atexit(biosReport);
	auto it = biosByPc.find(Sh4cntx.pc);
	if (it == biosByPc.end())
		biosByPc[Sh4cntx.pc] = { p, p };
	else
	{
		it->second.first = std::min(it->second.first, p);
		it->second.second = std::max(it->second.second, p);
	}
}
u8 *aot_ram;
u32 aot_ram_mask;

static void skipOptional(u32 addr, u32 size, bool write)
{
	// skip constant-folded literal reads the AOT code didn't perform
	while (journalPos < journal.size() && journal[journalPos].optional
			&& (write || journal[journalPos].addr != addr || journal[journalPos].size != size))
		journalPos++;
}

u64 aot_replay_read(u32 addr, u32 size)
{
	skipOptional(addr, size, false);
	if (inCodePages(addr) && size <= 4 && (journalPos >= journal.size() || journal[journalPos].addr != addr))
		return size == 1 ? realRead8(addr) : size == 2 ? realRead16(addr) : realRead32(addr);
	if (journalPos >= journal.size() || journal[journalPos].write || journal[journalPos].addr != addr || journal[journalPos].size != size)
	{
		replayFail("AOT read", addr, size, 0);
		// best effort: serve the value if the next entry is a read of the same size
		if (journalPos < journal.size() && !journal[journalPos].write && journal[journalPos].size == size)
			return journal[journalPos++].value;
		return 0;
	}
	return journal[journalPos++].value;
}

void aot_replay_write(u32 addr, u32 size, u64 value)
{
	skipOptional(addr, size, true);
	if (journalPos >= journal.size() || !journal[journalPos].write || journal[journalPos].addr != addr
			|| journal[journalPos].size != size || journal[journalPos].value != value)
	{
		replayFail("AOT write", addr, size, value);
		if (journalPos < journal.size() && journal[journalPos].write)
			journalPos++;
		return;
	}
	journalPos++;
}

void aot_update_intc()
{
	if (aot_verify_replay)
		intcPending = true;
	else
		UpdateINTC();
}

class Sh4AotExecutor : public Sh4Interpreter
{
	using super = Sh4Interpreter;
public:
	void Init() override
	{
		super::Init();
		// direct-mapped dispatch: page directory (vaddr >> 12) -> 2048 slot pointers per page
		pageDir.clear();
		pageDir.resize(1u << 20);
		size_t blockCount;
		const AotBlockEntry *blocks = aot_table(blockCount);
		for (size_t i = 0; i < blockCount; i++)
		{
			const AotBlockEntry& e = blocks[i];
			if (e.fn == nullptr)
				continue;
			std::unique_ptr<Slot *[]>& page = pageDir[e.vaddr >> 12];
			if (!page)
				page.reset(new Slot *[2048]());
			Slot *& sp = page[(e.vaddr & 0xfff) >> 1];
			if (sp == nullptr)
			{
				slots.emplace_back();
				sp = &slots.back();
			}
			sp->versions.push_back(&e);
		}
		INFO_LOG(DYNAREC, "AOT executor: %zu precompiled blocks at %zu addresses", blockCount, slots.size());
		trackPages = getenv("VNBA_AOT_NOPROTECT") == nullptr;
		if (const char *p = getenv("VNBA_AOT_MISSES"))
			missLog = fopen(p, "w");
		stats = getenv("VNBA_AOT_STATS") != nullptr;
		verify = getenv("VNBA_AOT_VERIFY") != nullptr;
		if (const char *p = getenv("VNBA_AOT_VERIFY_LOG"))
			verifyLog = fopen(p, "w");
	}

	void Term() override
	{
		if (cpuTime)
			fprintf(stderr, "AOT: time in system events (UpdateSystem_INTC): %.2f s, fallback blocks %llu, block runs %llu\n", sysTime,
					(unsigned long long)interpOps, (unsigned long long)blockRuns);
		if (stats)
			fprintf(stderr, "AOT stats: %llu block runs, %llu interpreted ops, %zu distinct miss PCs, %llu hash failures, %llu verified, %llu mismatches\n",
					(unsigned long long)blockRuns, (unsigned long long)interpOps, misses.size(), (unsigned long long)hashFails,
					(unsigned long long)verified, (unsigned long long)mismatches);
		if (verifyLog)
			fclose(verifyLog);
		if (missLog)
			fclose(missLog);
		missLog = nullptr;
		super::Term();
	}

	void ResetCache() override
	{
		// guest code may have been replaced: re-verify every block before running it again
		for (Slot& slot : slots)
			slot.cur = nullptr;
	}

	void Reset(bool hard) override
	{
		super::Reset(hard);
		resetAotState();
		aot_boot_rearm();
		bootPending = aot_boot_pending();
	}

	// Forget block selections and page tracking (after reset or a machine state change)
	void resetAotState()
	{
		ramMask = RAM_MASK;
		u32 pages = (RAM_SIZE >> AOT_PAGE_SHIFT);
		for (u32 i = 0; i < pageLocked.size(); i++)
			if (pageLocked[i])
				bm_UnlockPage(i << AOT_PAGE_SHIFT, 1 << AOT_PAGE_SHIFT);
		pageGen.assign(pages, 1);
		pageFaults.assign(pages, 0);
		pageLocked.assign(pages, false);
		aot_ram_write_hook = trackPages ? aotRamWrite : nullptr;
		aot_ram = addrspace::virtmemEnabled() ? addrspace::ram_base + 0x0C000000 : &mem_b[0];
		aot_ram_mask = RAM_MASK;
		aot_module_sync();	// the game module keeps its own copy of the RAM pointer
		ResetCache();
		if (getenv("VNBA_WATCH_OVERLAY") != nullptr && trackPages)
			for (u32 p = 0x20; p < 0x60; p++)
			{
				pageLocked[p] = true;
				bm_LockPage(p << AOT_PAGE_SHIFT, 1 << AOT_PAGE_SHIFT);
			}
	}

	void Run() override
	{
		Instance = this;
		VnbaCpuTimer _vnbaTimer;
		ctx->restoreHostRoundingMode();
		try {
			do
			{
				try {
					do
					{
						// do-while, like the dynarec main loop: always run at least one block
						do
							step();
						while (ctx->cycle_counter > 0);
						ctx->cycle_counter += SH4_TIMESLICE;
						if (AOT_UNLIKELY(overlayDir != nullptr))
							watchOverlay();
						if (AOT_UNLIKELY(!pokes.empty()))
							applyPokes();
						{
							extern bool aot_ws_active;
							void aot_ws_tick();
							if (AOT_UNLIKELY(aot_ws_active))
								aot_ws_tick();
						}
						if (AOT_UNLIKELY(peekOn))
							peek();
						if (AOT_UNLIKELY(ramDumpAt >= 0) && (long long)slices++ == ramDumpAt)
						{
							// RAM dump through the CPU's view of memory (diagnostics)
							if (FILE *f = fopen(getenv("VNBA_RAMDUMP_FILE"), "wb"))
							{
								for (u32 a = 0x0C000000; a < 0x0C000000 + RAM_SIZE; a += 4)
								{
									u32 v = addrspace::read32(a);
									fwrite(&v, 4, 1, f);
								}
								fclose(f);
							}
						}
						if (AOT_UNLIKELY(cpuTime))
						{
							auto t0 = std::chrono::steady_clock::now();
							UpdateSystem_INTC();
							VnbaCpuTimer::sys() += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
						}
						else
							UpdateSystem_INTC();
					} while (ctx->CpuRunning);
				} catch (const SH4ThrownException& ex) {
					Do_Exception(ex.epc, ex.expEvn);
					ctx->cycle_counter -= 5;
				}
			} while (ctx->CpuRunning);
		} catch (const debugger::Stop&) {
		}
		ctx->CpuRunning = false;
		Instance = nullptr;
		if (missLog)
			fflush(missLog);
	}

private:
	// Diagnostics for BIOS-free boot: report BIOS ROM execution after the game entry point
	bool biosWatch = getenv("VNBA_BIOS_WATCH") != nullptr;
	bool bootPending = true;
	const unsigned long long saveAt = getenv("VNBA_SAVE_AT") && getenv("VNBA_SAVE_FILE") ? atoll(getenv("VNBA_SAVE_AT")) * (SH4_MAIN_CLOCK / 60) : 0;	// frames -> cycles
	const unsigned long long staticAfter = getenv("VNBA_AOT_STATIC_AFTER") && getenv("VNBA_AOT_STATIC") ? atoll(getenv("VNBA_AOT_STATIC_AFTER")) * (SH4_MAIN_CLOCK / 60) : 0;	// frames -> cycles
	bool pastEntry = false;
	void watchBios(u32 pc)
	{
		if (!pastEntry && aot_game_info().valid && (pc & 0x1FFFFFFF) == aot_game_info().entry)
		{
			pastEntry = true;
			aot_bios_read_watch = true;
			fprintf(stderr, "BIOSWATCH: reached game entry %08x at block run %llu\n", aot_game_info().entry, (unsigned long long)blockRuns);
		}
		static int reports;
		if (pastEntry && (pc & 0x1FFFFFFF) < 0x00200000 && reports++ < 20)
			fprintf(stderr, "BIOSWATCH: executing BIOS ROM at %08x (pr %08x)\n", pc, ctx->pr);
	}

	// Diagnostics: snapshot the overlay area 0x0C020000-0x0C05FFFF whenever its content changes
	const char *overlayDir = getenv("VNBA_OVERLAY_DIR");
	u32 overlayHash = 0;
	u64 overlaySlices = 0;
	void watchOverlay()
	{
		if ((overlaySlices++ & 63) != 0)
			return;
		std::vector<u8> buf(0x40000);
		for (u32 i = 0; i < buf.size(); i += 4)
		{
			u32 v = addrspace::read32(0x0C020000 + i);
			memcpy(&buf[i], &v, 4);
		}
		u32 h = 2166136261u;
		for (u8 b : buf)
			h = (h ^ b) * 16777619u;
		if (h == overlayHash)
			return;
		overlayHash = h;
		char path[512];
		snprintf(path, sizeof(path), "%s/overlay_%08x.bin", overlayDir, h);
		if (FILE *f = fopen(path, "wb")) { fwrite(buf.data(), 1, buf.size(), f); fclose(f); }
		snprintf(path, sizeof(path), "%s/overlay_log.txt", overlayDir);
		if (FILE *f = fopen(path, "a")) { fprintf(f, "slice %llu hash %08x pc %08x pr %08x\n", (unsigned long long)overlaySlices, h, ctx->pc, ctx->pr); fclose(f); }
	}

	// Experiments: VNBA_POKE="addr=f:value,addr=u:value" forces guest RAM words every timeslice
	struct Poke { u32 addr, value; };
	std::vector<Poke> pokes = parsePokes();
	bool peekOn = getenv("VNBA_PEEK") != nullptr;
	long long ramDumpAt = getenv("VNBA_RAMDUMP_AT") ? atoll(getenv("VNBA_RAMDUMP_AT")) : -1;
	u64 slices = 0;
	static std::vector<Poke> parsePokes()
	{
		std::vector<Poke> v;
		const char *e = getenv("VNBA_POKE");
		if (e == nullptr)
			return v;
		std::string s = e;
		size_t p = 0;
		while (p < s.size())
		{
			size_t c = s.find(',', p);
			if (c == std::string::npos) c = s.size();
			std::string it = s.substr(p, c - p);
			size_t eq = it.find('=');
			if (eq != std::string::npos && eq + 2 < it.size())
			{
				Poke pk;
				pk.addr = strtoul(it.substr(0, eq).c_str(), nullptr, 16);
				std::string val = it.substr(eq + 3);
				if (it[eq + 1] == 'f') { float f = strtof(val.c_str(), nullptr); memcpy(&pk.value, &f, 4); }
				else pk.value = strtoul(val.c_str(), nullptr, 0);
				v.push_back(pk);
			}
			p = c + 1;
		}
		return v;
	}
	void peek()
	{
		static const char *e = getenv("VNBA_PEEK");
		static u64 n;
		if (e == nullptr || (n++ % 200000) != 0)
			return;
		std::string s = e;
		size_t p = 0;
		fprintf(stderr, "PEEK");
		while (p < s.size())
		{
			size_t c = s.find(',', p);
			if (c == std::string::npos) c = s.size();
			u32 a = strtoul(s.substr(p, c - p).c_str(), nullptr, 16);
			u32 v = addrspace::read32(a);
			float f; memcpy(&f, &v, 4);
			fprintf(stderr, " %08x=%08x(%g)", a, v, f);
			p = c + 1;
		}
		fprintf(stderr, "\n");
	}
	void applyPokes()
	{
		for (const Poke& pk : pokes)
			addrspace::write32(pk.addr, pk.value);
	}

	// VNBA_PROF=from,to : per-block execution counts for FrameCount in [from,to]
	bool profOn = getenv("VNBA_PROF") != nullptr;
	std::unordered_map<u32, u64> profCounts;
	void profile(u32 pc)
	{
		static u32 from, to;
		static bool init, done;
		if (!init) { init = true; sscanf(getenv("VNBA_PROF"), "%u,%u", &from, &to); }
		if (done || FrameCount < from) return;
		if (FrameCount > to)
		{
			done = true;
			FILE *f = fopen(getenv("VNBA_PROF_FILE") ? getenv("VNBA_PROF_FILE") : "prof.txt", "w");
			for (auto& kv : profCounts) fprintf(f, "%08X %llu\n", kv.first, (unsigned long long)kv.second);
			fclose(f);
			return;
		}
		profCounts[pc]++;
	}

	inline void runBlock(const AotBlockEntry& e)
	{
		blockRuns++;
		aot_cur_block = e.vaddr;
		if (AOT_UNLIKELY(e.vaddr == aot_hook_pc || e.vaddr == aot_hook_pc2))
		{
			e.fn(ctx);
			aot_run_hook(e.vaddr, ctx);
			return;
		}
		if (AOT_UNLIKELY(profOn))
			profile(e.vaddr);
		if (AOT_UNLIKELY(verify))
			verifyBlock(e);
		else if (AOT_UNLIKELY(cpuTime))
		{
			u64 t0 = __rdtsc();
			e.fn(ctx);
			VnbaCpuTimer::blockTsc() += __rdtsc() - t0;
		}
		else
			e.fn(ctx);
	}

	// Page generation for the block at pc (0 = untracked: must hash-check every run)
	static inline u32 blockGen(u32 pc, u32 size)
	{
		if (!trackPages || !IsOnRam(pc) || (pc & 0x1FFF0000) == 0x0C000000)
			return (pc & 0x1FFFFFFF) < 0x00200000 ? 1 : 0;	// boot ROM never changes
		u32 p0 = (pc & ramMask) >> AOT_PAGE_SHIFT;
		u32 p1 = ((pc + size - 1) & ramMask) >> AOT_PAGE_SHIFT;
		if (pageFaults[p0] >= MAX_PAGE_FAULTS || pageFaults[p1] >= MAX_PAGE_FAULTS)
			return 0;
		return pageGen[p0] + (p1 != p0 ? pageGen[p1] << 16 : 0);
	}

	static inline void lockPages(u32 pc, u32 size)
	{
		if (!trackPages || !IsOnRam(pc) || (pc & 0x1FFF0000) == 0x0C000000)
			return;
		for (u32 p = (pc & ramMask) >> AOT_PAGE_SHIFT; p <= ((pc + size - 1) & ramMask) >> AOT_PAGE_SHIFT; p++)
			if (!pageLocked[p] && pageFaults[p] < MAX_PAGE_FAULTS)
			{
				pageLocked[p] = true;
				bm_LockPage(p << AOT_PAGE_SHIFT, 1 << AOT_PAGE_SHIFT);
			}
	}

	// Pick the precompiled version that matches the guest code currently in memory
	const AotBlockEntry *select(Slot& s, u32 pc, u32 fpu)
	{
		u32 lastSize = 0, h = 0;
		for (const AotBlockEntry *e : s.versions)
		{
			if (e->fpu != fpu)
				continue;
			if (e->size != lastSize)
			{
				h = fnv1a(pc, e->size);
				lastSize = e->size;
			}
			if (h == e->hash)
				return e;
		}
		return nullptr;
	}

	inline void step()
	{
		if (AOT_UNLIKELY(bootPending))
		{
			if (aot_boot_hook(ctx->pc))
			{
				// whole machine state replaced: drop all block/page state
				resetAotState();
				aot_bios_read_watch = true;	// warn about BIOS reads outside the saved windows
				if (const char *seeds = getenv("VNBA_AOT_STATIC"); seeds != nullptr && getenv("VNBA_AOT_STATIC_AFTER") == nullptr)
				{
					// offline coverage generation: the program is in RAM now
					void aot_static_generate(const char *seedFile);
					aot_static_generate(seeds);
					if (const char *outDir = getenv("VNBA_GENERATE"))
					{
						// builder: turn the dump into the game module sources
						void aot_dump_close();
						aot_dump_close();
						bool aot_split_dump(const char *dumpPath, const char *outDir, int units);
						bool ok = aot_split_dump(getenv("VNBA_AOT_DUMP"), outDir, 32);
						fflush(stderr);
						std::_Exit(ok ? 0 : 2);
					}
					exit(0);
				}
			}
			bootPending = aot_boot_pending();
		}
		if (AOT_UNLIKELY(saveAt != 0) && !bootPending && sh4_sched_now64() >= saveAt)
		{
			void aot_boot_save_snapshot(const char *file);
			aot_boot_save_snapshot(getenv("VNBA_SAVE_FILE"));
			exit(0);
		}
		if (AOT_UNLIKELY(staticAfter != 0) && !bootPending && sh4_sched_now64() >= staticAfter)
		{
			// offline coverage generation once the game has loaded its final code
			void aot_static_generate(const char *seedFile);
			aot_static_generate(getenv("VNBA_AOT_STATIC"));
			exit(0);
		}
		const u32 pc = ctx->pc;
		if (AOT_UNLIKELY(biosWatch))
			watchBios(pc);
		const u32 fpu = ctx->fpscr.full & FPU_MASK;
		Slot *const *page = pageDir[pc >> 12].get();
		Slot *sp = page != nullptr ? page[(pc & 0xfff) >> 1] : nullptr;
		if (sp != nullptr)
		{
			Slot& s = *sp;
			const AotBlockEntry *e = s.cur;
			if (AOT_LIKELY(e != nullptr && e->fpu == fpu))
			{
				u32 gen = blockGen(pc, e->size);
				if (AOT_LIKELY(gen != 0 && gen == s.gen))
				{
					runBlock(*e);
					return;
				}
			}
			e = select(s, pc, fpu);
			u32 gen = 0;
			if (e != nullptr)
			{
				lockPages(pc, e->size);
				gen = blockGen(pc, e->size);
			}
			else if (s.cur != nullptr)
				hashFails++;
			s.cur = e;
			s.gen = gen;
			if (e != nullptr)
			{
				runBlock(*e);
				return;
			}
		}
		// no precompiled block for the current guest code
		misses.insert(key(pc, fpu));
		runFallback(pc, fpu);
	}

	// Interpret the guest instructions of one block [vaddr, vaddr + size)
	void interpretBlock(u32 vaddr, u32 size, bool withTrace)
	{
		const u32 end = vaddr + size;
		if (withTrace)
			trace.clear();
		for (int guard = 0; guard < 4096; guard++)
		{
			u32 p = ctx->pc;
			if (withTrace)
				trace.push_back(p);
			u16 op = IReadMem16(p);
			u32 t = OpDesc[op]->type;
			u32 len = ((t & WritesPC) && (t & Delayslot)) ? 4 : 2;
			ExecuteOpcode(ReadNexOp());
			if (ctx->pc < vaddr || ctx->pc >= end)
				break;
			// a not-taken delayed conditional branch leaves its delay slot to the next step
			if (p + len >= end && !(len == 4 && ctx->pc == p + 2))
				break;
		}
	}

	// Cycle-exact fallback for code without a precompiled block: decode the block exactly as the
	// dynarec would (to get its size, cycle cost and end type), then interpret its instructions.
	struct Fallback {
		u32 hash = 0, size = 0, cycles = 0;
		bool intr = false;
	};
	void runFallback(u32 pc, u32 fpu)
	{
		Fallback& fb = fallbacks[key(pc, fpu)];
		if (fb.size == 0 || fnv1a(pc, fb.size) != fb.hash)
		{
			RuntimeBlockInfo rbi;
			if (!rbi.Setup(pc, ctx->fpscr) || rbi.sh4_code_size == 0)
			{
				// exception raised while decoding: let the interpreter handle it
				interpOps++;
				ExecuteOpcode(ReadNexOp());
				return;
			}
			aot_dump_block(&rbi);	// emits C++ for the missing block when VNBA_AOT_DUMP is set
			fb.size = rbi.sh4_code_size;
			fb.hash = fnv1a(pc, fb.size);
			fb.cycles = rbi.guest_cycles;
			fb.intr = rbi.BlockType == BET_StaticIntr || rbi.BlockType == BET_DynamicIntr;
			if (missLog)
				fprintf(missLog, "%08x %06x %08x %u frame-run %llu\n", pc, fpu, fb.hash, fb.size, (unsigned long long)blockRuns);
		}
		const int cc = ctx->cycle_counter - fb.cycles;	// the dynarec charges the whole block up front
		ctx->cycle_counter = cc;
		interpretBlock(pc, fb.size, false);
		ctx->cycle_counter = cc;
		interpOps++;
		VnbaCpuTimer::fallbacks()++;
		if (fb.intr)
			UpdateINTC();
	}

	// Run the block on the interpreter (recording memory accesses), then re-run the AOT
	// version from the same state replaying those accesses, and compare the results.
	void verifyBlock(const AotBlockEntry& e)
	{
		if (realRead8 == nullptr) {
			realRead8 = ReadMem8; realRead16 = ReadMem16; realRead32 = ReadMem32; realRead64 = ReadMem64;
			realWrite8 = WriteMem8; realWrite16 = WriteMem16; realWrite32 = WriteMem32; realWrite64 = WriteMem64;
		}
		alignas(64) static Sh4Context snap, ref;
		memcpy(&snap, ctx, sizeof(Sh4Context));
		realSqWrite = ctx->doSqWrite;

		// 1. reference: interpreter
		journal.clear();
		codePageLo = e.vaddr >> 12;
		codePageHi = (e.vaddr + e.size - 1) >> 12;
		setMemHandlers(1);
		ctx->doSqWrite = recSqWrite;
		try {
			interpretBlock(e.vaddr, e.size, true);
		} catch (...) {
			setMemHandlers(0);
			ctx->doSqWrite = realSqWrite;
			throw;
		}
		ctx->doSqWrite = realSqWrite;
		memcpy(&ref, ctx, sizeof(Sh4Context));

		// 2. AOT, replaying the journal
		memcpy(ctx, &snap, sizeof(Sh4Context));
		setMemHandlers(2);
		ctx->doSqWrite = repSqWrite;
		journalPos = 0;
		replayError = false;
		intcPending = false;
		aot_verify_replay = true;
		e.fn(ctx);
		aot_verify_replay = false;
		setMemHandlers(0);
		ctx->doSqWrite = realSqWrite;
		while (journalPos < journal.size() && journal[journalPos].optional)
			journalPos++;
		if (!replayError && journalPos != journal.size())
		{
			replayError = true;
			replayErrorMsg = "AOT made " + std::to_string(journalPos) + " accesses, interpreter " + std::to_string(journal.size());
		}

		// 3. compare
		std::string diff = compare(ref, *ctx);
		verified++;
		if (replayError || !diff.empty())
		{
			mismatches++;
			if (reported.insert(key(e.vaddr, e.fpu)).second && verifyLog)
			{
				fprintf(verifyLog, "MISMATCH block %08x fpu %06x size %u: %s %s\n", e.vaddr, e.fpu, e.size,
						replayError ? replayErrorMsg.c_str() : "", diff.c_str());
				fprintf(verifyLog, "  code:");
				for (u32 a = e.vaddr; a < e.vaddr + e.size + 4; a += 2)
				{
					u16 op = IReadMem16(a);
					char dis[64];
					OpDesc[op]->Disassemble(dis, a, op);
					fprintf(verifyLog, " [%08x %04x %s]", a, op, dis);
				}
				fprintf(verifyLog, "\n  interp pcs:");
				for (u32 a : trace)
					fprintf(verifyLog, " %08x", a);
				fprintf(verifyLog, "\n");
				fflush(verifyLog);
			}
		}
		// continue from the reference (interpreter) state
		int cycles = ctx->cycle_counter;
		memcpy(ctx, &ref, sizeof(Sh4Context));
		ctx->cycle_counter = cycles;
		if (intcPending)
			UpdateINTC();
	}

	static std::string compare(const Sh4Context& a, const Sh4Context& b)
	{
		std::string d;
		char buf[96];
		auto cmp = [&](const char *name, u32 x, u32 y) {
			if (x != y) { snprintf(buf, sizeof(buf), " %s:%08x/%08x", name, x, y); d += buf; }
		};
		for (int i = 0; i < 16; i++) { snprintf(buf, sizeof(buf), "r%d", i); std::string n = buf; cmp(n.c_str(), a.r[i], b.r[i]); }
		for (int i = 0; i < 8; i++) { snprintf(buf, sizeof(buf), "rb%d", i); std::string n = buf; cmp(n.c_str(), a.r_bank[i], b.r_bank[i]); }
		for (int i = 0; i < 16; i++) {
			u32 x, y;
			memcpy(&x, &a.fr[i], 4); memcpy(&y, &b.fr[i], 4);
			snprintf(buf, sizeof(buf), "fr%d", i); std::string n = buf; cmp(n.c_str(), x, y);
			memcpy(&x, &a.xf[i], 4); memcpy(&y, &b.xf[i], 4);
			snprintf(buf, sizeof(buf), "xf%d", i); n = buf; cmp(n.c_str(), x, y);
		}
		cmp("macl", a.mac.l, b.mac.l); cmp("mach", a.mac.h, b.mac.h);
		cmp("gbr", a.gbr, b.gbr); cmp("ssr", a.ssr, b.ssr); cmp("spc", a.spc, b.spc); cmp("sgr", a.sgr, b.sgr);
		cmp("dbr", a.dbr, b.dbr); cmp("vbr", a.vbr, b.vbr); cmp("pr", a.pr, b.pr); cmp("fpul", a.fpul, b.fpul);
		cmp("pc", a.pc, b.pc); cmp("sr", a.sr.status, b.sr.status); cmp("T", a.sr.T, b.sr.T);
		cmp("fpscr", a.fpscr.full, b.fpscr.full);
		return d;
	}

	std::vector<std::unique_ptr<Slot *[]>> pageDir;
	std::deque<Slot> slots;	// stable addresses
	std::unordered_set<u64> misses;
	FILE *missLog = nullptr;
	std::unordered_map<u64, Fallback> fallbacks;
	bool stats = false;
	bool cpuTime = getenv("VNBA_CPU_TIME") != nullptr;
	double sysTime = 0;
	bool verify = false;
	FILE *verifyLog = nullptr;
	std::unordered_set<u64> reported;
	std::vector<u32> trace;
	u64 verified = 0, mismatches = 0;
	u64 blockRuns = 0, interpOps = 0, hashFails = 0;
};

// The AOT executor is used whenever precompiled blocks are linked in, unless VNBA_AOT=0.
// VNBA_AOT=1 forces it (all code then runs through the cycle-exact fallback).
bool aot_executor_enabled()
{
	static const bool enabled = [] {
		const char *v = getenv("VNBA_AOT");
		if (v != nullptr)
			return v[0] != '0';
		return aot_table_count() > 0;
	}();
	return enabled;
}

Sh4Executor *Get_Sh4AotExecutor()
{
	return new Sh4AotExecutor();
}
