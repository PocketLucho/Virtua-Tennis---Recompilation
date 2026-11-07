#pragma once
#include "types.h"
#include "hw/sh4/sh4_if.h"
#include "hw/mem/addrspace.h"
#include <cstring>

typedef void (*AotBlockFn)(Sh4Context *ctx);
struct AotBlockEntry {
	u32 vaddr;
	u32 fpu;	// fpscr & 0x00180003 (PR, SZ, RM)
	u32 hash;	// FNV-1a of the guest code bytes
	u32 size;	// guest code size in bytes
	AotBlockFn fn;
};
extern const AotBlockEntry aot_blocks[];
extern const size_t aot_block_count;
// Blocks in use: linked in, or from the game module (see aot_module.cpp)
const AotBlockEntry *aot_table(size_t& count);
size_t aot_table_count();
bool aot_module_loaded();
void aot_module_sync();

// Lockstep verification support: when aot_verify_replay is set, memory accesses made by
// generated code are served from / checked against the interpreter's access journal.
extern bool aot_verify_replay;
u64 aot_replay_read(u32 addr, u32 size);
void aot_replay_write(u32 addr, u32 size, u64 value);
void aot_update_intc();

#define AOT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define AOT_LIKELY(x) __builtin_expect(!!(x), 1)

// Direct RAM access. aot_ram points to the view of system RAM that code-page write
// protection applies to (the virtual address space view when virtmem is enabled).
extern bool aot_bios_read_watch;
void aot_bios_read(u32 a);
#define AOT_SLOW_READ_HOOK(a) do { if (AOT_UNLIKELY(aot_bios_read_watch)) aot_bios_read(a); } while (0)
extern u8 *aot_ram;
extern u32 aot_ram_mask;
static inline bool aot_is_ram(u32 a) { return ((a >> 26) & 7) == 3 && (a >> 29) != 7; }
template<typename T> static inline T aot_ram_ld(u32 a) { T v; memcpy(&v, aot_ram + (a & aot_ram_mask), sizeof(T)); return v; }
template<typename T> static inline void aot_ram_st(u32 a, T v) { memcpy(aot_ram + (a & aot_ram_mask), &v, sizeof(T)); }

static inline s32 aot_read8sx(u32 a) {
	if (AOT_UNLIKELY(aot_verify_replay)) return (s8)aot_replay_read(a, 1);
	if (AOT_LIKELY(aot_is_ram(a))) return aot_ram_ld<s8>(a);
	AOT_SLOW_READ_HOOK(a);
	return addrspace::read8SX32(a);
}
static inline s32 aot_read16sx(u32 a) {
	if (AOT_UNLIKELY(aot_verify_replay)) return (s16)aot_replay_read(a, 2);
	if (AOT_LIKELY(aot_is_ram(a))) return aot_ram_ld<s16>(a);
	AOT_SLOW_READ_HOOK(a);
	return addrspace::read16SX32(a);
}
static inline u32 aot_read32(u32 a) {
	if (AOT_UNLIKELY(aot_verify_replay)) return (u32)aot_replay_read(a, 4);
	if (AOT_LIKELY(aot_is_ram(a))) return aot_ram_ld<u32>(a);
	AOT_SLOW_READ_HOOK(a);
	return addrspace::read32(a);
}
static inline u64 aot_read64(u32 a) {
	if (AOT_UNLIKELY(aot_verify_replay)) return aot_replay_read(a, 8);
	if (AOT_LIKELY(aot_is_ram(a))) return aot_ram_ld<u64>(a);
	AOT_SLOW_READ_HOOK(a);
	return addrspace::read64(a);
}
static inline void aot_write8(u32 a, u8 v) {
	if (AOT_UNLIKELY(aot_verify_replay)) aot_replay_write(a, 1, v);
	else if (AOT_LIKELY(aot_is_ram(a))) aot_ram_st<u8>(a, v);
	else addrspace::write8(a, v);
}
static inline void aot_write16(u32 a, u16 v) {
	if (AOT_UNLIKELY(aot_verify_replay)) aot_replay_write(a, 2, v);
	else if (AOT_LIKELY(aot_is_ram(a))) aot_ram_st<u16>(a, v);
	else addrspace::write16(a, v);
}
static inline void aot_write32(u32 a, u32 v) {
	if (AOT_UNLIKELY(aot_verify_replay)) aot_replay_write(a, 4, v);
	else if (AOT_LIKELY(aot_is_ram(a))) aot_ram_st<u32>(a, v);
	else addrspace::write32(a, v);
}
static inline void aot_write64(u32 a, u64 v) {
	if (AOT_UNLIKELY(aot_verify_replay)) aot_replay_write(a, 8, v);
	else if (AOT_LIKELY(aot_is_ram(a))) aot_ram_st<u64>(a, v);
	else addrspace::write64(a, v);
}
