// Compiled only into the game module (vt_game.dll). Provides the engine symbols the generated
// blocks refer to, forwarding them to the host engine through AotHostApi.
#include "aot_dll_api.h"
#include "hw/mem/addrspace.h"
#include "hw/sh4/sh4_core.h"

#ifdef _WIN32
#define VNBA_EXPORT extern "C" __declspec(dllexport)
#else
#define VNBA_EXPORT extern "C" __attribute__((visibility("default")))
#endif

static AotHostApi host;

u8 *aot_ram;
u32 aot_ram_mask;
bool aot_verify_replay;	// lockstep verification is not available with the game module
bool aot_bios_read_watch;
OpCallFP *OpPtr[0x10000];
f32_x2 sin_table[0x10000];

u64 aot_replay_read(u32 addr, u32 size) { return host.replayRead(addr, size); }
void aot_replay_write(u32 addr, u32 size, u64 value) { host.replayWrite(addr, size, value); }
void aot_update_intc() { host.updateIntc(); }
void aot_bios_read(u32 a) { host.biosRead(a); }
void Do_Exception(u32 epc, Sh4ExceptionCode expEvn) { host.doException(epc, expEvn); }
bool UpdateSR() { return host.updateSR(); }
void DYNACALL Sh4Context::UpdateFPSCR(Sh4Context *ctx) { host.updateFPSCR(ctx); }

namespace addrspace
{
template<typename T> T DYNACALL readt(u32 addr)
{
	if constexpr (sizeof(T) == 1)
		return (T)host.read8(addr);
	else if constexpr (sizeof(T) == 2)
		return (T)host.read16(addr);
	else if constexpr (sizeof(T) == 4)
		return (T)host.read32(addr);
	else
		return (T)host.read64(addr);
}
template u8 DYNACALL readt<u8>(u32 addr);
template u16 DYNACALL readt<u16>(u32 addr);
u32 DYNACALL read32(u32 addr) { return host.read32(addr); }
u64 DYNACALL read64(u32 addr) { return host.read64(addr); }
void DYNACALL write8(u32 addr, u8 data) { host.write8(addr, data); }
void DYNACALL write16(u32 addr, u16 data) { host.write16(addr, data); }
void DYNACALL write32(u32 addr, u32 data) { host.write32(addr, data); }
void DYNACALL write64(u32 addr, u64 data) { host.write64(addr, data); }
}

VNBA_EXPORT const AotBlockEntry *vnba_game_init(const AotHostApi *api, size_t *count)
{
	if (api == nullptr || api->version != VNBA_GAME_API_VERSION || api->contextSize != sizeof(Sh4Context))
		return nullptr;
	host = *api;
	aot_ram = api->ram;
	aot_ram_mask = api->ramMask;
	memcpy(OpPtr, api->opPtr, sizeof(OpPtr));
	memcpy(sin_table, api->sinTable, sizeof(sin_table));
	*count = aot_block_count;
	return aot_blocks;
}
