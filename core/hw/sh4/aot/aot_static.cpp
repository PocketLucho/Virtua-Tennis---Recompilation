// Static AOT coverage: decode every block reachable from a set of seed addresses in the
// game program (0x0C020000-0x0C21FFFF, loaded verbatim from the cartridge by the BIOS)
// and emit C++ for all of them through the regular AOT dump.
//
// Seeds: a text file (VNBA_AOT_STATIC=path) with one hex address per line, plus every
// 32-bit word in the program that points into the program (code pointers in literal pools
// and jump tables). Block successors (branch targets, fall-through, return addresses after
// calls) are followed recursively. Each block is generated for the FPU configurations seen
// at runtime (SZ=0 and SZ=1 with round-to-zero) when it contains FPU instructions.
#include "types.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "aot_game.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <deque>
#include <vector>

void aot_dump_block(RuntimeBlockInfo *block);

namespace {


inline bool inProgram(u32 a) { return aot_game_in_program(a); }
}

// Trial-decode everything reachable from a pointer target; accept it only if no block contains
// an undefined opcode (data decoded as code almost always does).
static bool plausibleCode(u32 start, std::set<u32>& known)
{
	fpscr_t cfg;
	cfg.full = 0x00040001;
	std::deque<u32> work{ start };
	std::set<u32> local{ start };
	int budget = 400;
	while (!work.empty())
	{
		u32 pc = work.front();
		work.pop_front();
		if (known.count(pc) != 0)
			continue;	// joins already accepted code
		if (--budget < 0)
			return true;	// large valid-looking region
		RuntimeBlockInfo rbi;
		try {
			if (!rbi.Setup(pc, cfg) || rbi.sh4_code_size == 0)
				return false;
		} catch (...) {
			return false;
		}
		for (u32 a = pc; a < pc + rbi.sh4_code_size; a += 2)
			if (strcmp(OpDesc[IReadMem16(a)]->diss, "missing") == 0)
				return false;
		for (u32 n : { rbi.BranchBlock, rbi.NextBlock,
				(rbi.BlockType == BET_StaticCall || rbi.BlockType == BET_DynamicCall) ? rbi.vaddr + rbi.sh4_code_size : 0xFFFFFFFFu })
			if (inProgram(n) && local.insert(n & 0x1FFFFFFF).second)
				work.push_back(n & 0x1FFFFFFF);
	}
	return true;
}

extern bool aot_static_generating;

void aot_static_generate(const char *seedFile)
{
	aot_static_generating = true;
	std::deque<u32> work;
	std::set<u32> seen;
	auto push = [&](u32 a) {
		if (a == 0xFFFFFFFF || !inProgram(a))
			return;
		a = (a & 0x1FFFFFFF);	// game code runs from the P0 addresses
		if (seen.insert(a).second)
			work.push_back(a);
	};
	size_t fileSeeds = 0, ptrSeeds = 0;
	// code outside the program image (copied to low RAM at boot, run from P0/P1/P2):
	// only the listed entry points, generated at the exact address they run from
	std::vector<u32> rawSeeds;
	if (FILE *f = fopen(seedFile, "r"))
	{
		unsigned a;
		while (fscanf(f, "%x%*[^\n]", &a) == 1)
		{
			if (inProgram(a))
				push(a);
			else if (((a >> 26) & 7) == 3 && (a >> 29) != 7 && (a & 0x01FFFFFF) < (aot_game_info().progStart & 0x01FFFFFF))
				rawSeeds.push_back(a);
			fileSeeds++;
		}
		fclose(f);
	}
	push(aot_game_info().entry);
	fpscr_t cfg[2];
	cfg[0].full = 0x00040001;	// PR=0 SZ=0 RM=1 (game default)
	cfg[1].full = 0x00140001;	// SZ=1 (64-bit fmov) also used by the game
	size_t blocks = 0, fpuBlocks = 0, failed = 0;
	auto drain = [&]() {
	while (!work.empty())
		{
			u32 pc = work.front();
			work.pop_front();
			for (int c = 0; c < 2; c++)
			{
				RuntimeBlockInfo rbi;
				bool ok = false;
				try {
					ok = rbi.Setup(pc, cfg[c]);
				} catch (...) {
					ok = false;
				}
				if (!ok || rbi.sh4_code_size == 0)
				{
					failed++;
					break;
				}
				if (c == 1 && (!rbi.has_fpu_op || getenv("VNBA_AOT_STATIC_SZ1") == nullptr))
					break;
				aot_dump_block(&rbi);
				blocks++;
				if (c == 1)
					fpuBlocks++;
				if (c == 0)
				{
					push(rbi.BranchBlock);
					push(rbi.NextBlock);
					if (rbi.BlockType == BET_StaticCall || rbi.BlockType == BET_DynamicCall)
						push(rbi.vaddr + rbi.sh4_code_size);	// return address
				}
				if (!rbi.has_fpu_op)
					break;
			}
		}
	};
	drain();
	if (getenv("VNBA_AOT_STATIC_PTR") != nullptr)
		for (u32 a = aot_game_info().progStart; a < aot_game_info().progEnd; a += 4)
		{
			u32 v = ReadMem32_nommu(a);
			if (inProgram(v) && seen.count(v & 0x1FFFFFFF) == 0 && plausibleCode(v & 0x1FFFFFFF, seen))
			{
				size_t before = seen.size();
				push(v);
				ptrSeeds += seen.size() - before;
			}
		}
	drain();
	// Function starts: code right after "rts; <delay slot>" (skipping nop padding) that decodes
	// cleanly is very likely the next function. Catches functions only reached through
	// computed addresses that are not stored as plain pointers.
	size_t funcSeeds = 0;
	if (getenv("VNBA_AOT_STATIC_FUNCS") != nullptr)
	{
		for (int pass = 0; pass < 4; pass++)
		{
			size_t before = funcSeeds;
			for (u32 a = aot_game_info().progStart; a + 4 < aot_game_info().progEnd; a += 2)
			{
				if (IReadMem16(a) != 0x000B)	// rts
					continue;
				u32 f = a + 4;
				while (f < aot_game_info().progEnd && IReadMem16(f) == 0x0009)	// nop padding
					f += 2;
				if (f >= aot_game_info().progEnd || seen.count(f) != 0 || !plausibleCode(f, seen))
					continue;
				push(f);
				funcSeeds++;
			}
			drain();
			if (funcSeeds == before)
				break;
		}
	}
	fprintf(stderr, "AOT static: %zu function starts, ", funcSeeds);
	for (u32 pc : rawSeeds)
		for (int c = 0; c < 2; c++)
		{
			RuntimeBlockInfo rbi;
			bool ok = false;
			try {
				ok = rbi.Setup(pc, cfg[c]);
			} catch (...) {
				ok = false;
			}
			if (!ok || rbi.sh4_code_size == 0)
			{
				failed++;
				break;
			}
			aot_dump_block(&rbi);
			blocks++;
			if (!rbi.has_fpu_op)
				break;
		}
	aot_static_generating = false;
	fprintf(stderr, "AOT static: %zu seeds from file, %zu code pointers, %zu addresses, %zu blocks (%zu SZ=1 variants), %zu undecodable\n",
			fileSeeds, ptrSeeds, seen.size(), blocks, fpuBlocks, failed);
}


// Split an AOT dump into compilable translation units plus the block table
// (same output as tools/aot_split.py --exclude-bios).
#include <map>
#include <string>
#include <tuple>
bool aot_split_dump(const char *dumpPaths, const char *outDir, int units)
{
	// dumpPaths: one dump, or several separated by ';' (one per game) merged into one module
	struct Block { u32 vaddr, fpu, hash, size; std::string name, orig, code; };
	std::map<std::tuple<u32, u32, u32>, Block> blocks;
	std::string pathList = dumpPaths;
	size_t pos = 0;
	while (pos <= pathList.size())
	{
	size_t sep = pathList.find(';', pos);
	if (sep == std::string::npos)
		sep = pathList.size();
	const std::string dumpPath = pathList.substr(pos, sep - pos);
	pos = sep + 1;
	if (dumpPath.empty())
		continue;
	FILE *in = fopen(dumpPath.c_str(), "r");
	if (in == nullptr)
		return false;
	Block *cur = nullptr;
	char *line = nullptr;
	std::vector<char> buf(1 << 16);
	std::string l;
	auto replaceFirst = [](std::string& s, const std::string& from, const std::string& to) {
		size_t p = s.find(from);
		if (p != std::string::npos)
			s.replace(p, from.size(), to);
	};
	while (fgets(buf.data(), (int)buf.size(), in) != nullptr)
	{
		l = buf.data();
		while (!l.empty() && l.back() != '\n' && fgets(buf.data(), (int)buf.size(), in) != nullptr)
			l += buf.data();
		if (l.compare(0, 9, "//@BLOCK ") == 0)
		{
			unsigned va, fpu, h, size;
			char name[128];
			cur = nullptr;
			if (sscanf(l.c_str(), "//@BLOCK %8x %6x %8x %u %127s", &va, &fpu, &h, &size, name) != 5)
				continue;
			if ((va & 0x1FFFFFFF) < 0x00200000)
				continue;	// BIOS code: never generated
			auto key = std::make_tuple((u32)va, (u32)fpu, (u32)h);
			if (blocks.count(key) != 0)
				continue;
			Block b{ va, fpu, h, size, name, name, "" };
			char suffix[16];
			snprintf(suffix, sizeof(suffix), "_%08X", h);
			if (b.name.size() < 9 || b.name.compare(b.name.size() - 9, 9, suffix) != 0)
				b.name += suffix;
			cur = &(blocks[key] = b);
			continue;
		}
		if (l.compare(0, 3, "//!") == 0)
		{
			cur = nullptr;
			continue;
		}
		if (cur != nullptr)
		{
			replaceFirst(l, "static void " + cur->orig, "void " + cur->name);
			replaceFirst(l, "\tUpdateINTC();", "\taot_update_intc();");
			cur->code += l;
		}
	}
	(void)line;
	fclose(in);
	}
	std::vector<const Block *> items;
	for (auto& kv : blocks)
		items.push_back(&kv.second);	// map order = (vaddr, fpu, hash)
	size_t per = (items.size() + units - 1) / units;
	for (int u = 0; u < units; u++)
	{
		char path[1024];
		snprintf(path, sizeof(path), "%s/aot_gen_%02d.cpp", outDir, u);
		FILE *f = fopen(path, "w");
		if (f == nullptr)
			return false;
		fputs("// Generated from your ROM by VirtuaTennis-Recompiled -- do not distribute\n#include \"hw/sh4/aot/aot_prelude.h\"\n\n", f);
		for (size_t i = u * per; i < std::min(items.size(), (u + 1) * per); i++)
			fputs(items[i]->code.c_str(), f);
		fclose(f);
	}
	char path[1024];
	snprintf(path, sizeof(path), "%s/aot_gen_table.cpp", outDir);
	FILE *f = fopen(path, "w");
	if (f == nullptr)
		return false;
	fputs("// Generated from your ROM by VirtuaTennis-Recompiled -- do not distribute\n#include \"hw/sh4/aot/aot_runtime.h\"\n\n", f);
	for (const Block *b : items)
		fprintf(f, "void %s(Sh4Context *);\n", b->name.c_str());
	fputs("\nconst AotBlockEntry aot_blocks[] = {\n", f);
	for (const Block *b : items)
		fprintf(f, "\t{ 0x%08Xu, 0x%06Xu, 0x%08Xu, %uu, %s },\n", b->vaddr, b->fpu, b->hash, b->size, b->name.c_str());
	fputs("};\nconst size_t aot_block_count = sizeof(aot_blocks) / sizeof(aot_blocks[0]);\n", f);
	fclose(f);
	fprintf(stderr, "AOT split: %zu blocks in %d units -> %s\n", items.size(), units, outDir);
	return true;
}
