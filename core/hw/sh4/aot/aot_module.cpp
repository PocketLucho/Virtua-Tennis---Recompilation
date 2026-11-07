// Engine side of the game module: finds the recompiled blocks either linked into the
// executable (developer builds) or in vt_game.dll next to it (distributable builds).
#include "aot_dll_api.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_mem.h"
#include "log/Log.h"
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#include <climits>
#endif

static const AotBlockEntry *tableBlocks;
static size_t tableCount;
static VnbaGameInitFn moduleInit;
static std::string modulePath;
static bool loaded;

static std::string exeDir()
{
#ifdef _WIN32
	wchar_t buf[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	std::wstring w(buf, n);
	size_t p = w.find_last_of(L"\\/");
	w = p == std::wstring::npos ? L"." : w.substr(0, p);
	int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string s(len > 0 ? len - 1 : 0, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
	return s;
#else
	char buf[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0)
		return ".";
	std::string s(buf, n);
	size_t p = s.find_last_of('/');
	return p == std::string::npos ? "." : s.substr(0, p);
#endif
}

static bool updateSRHost() { return UpdateSR(); }
static void doExceptionHost(u32 epc, Sh4ExceptionCode code) { Do_Exception(epc, code); }

static void fillApi(AotHostApi& api)
{
	api.version = VNBA_GAME_API_VERSION;
	api.contextSize = sizeof(Sh4Context);
	api.ram = aot_ram;
	api.ramMask = aot_ram_mask;
	api.opPtr = OpPtr;
	api.sinTable = sin_table;
	api.read8 = &addrspace::readt<u8>;
	api.read16 = &addrspace::readt<u16>;
	api.read32 = &addrspace::read32;
	api.read64 = &addrspace::read64;
	api.write8 = &addrspace::write8;
	api.write16 = &addrspace::write16;
	api.write32 = &addrspace::write32;
	api.write64 = &addrspace::write64;
	api.doException = doExceptionHost;
	api.updateSR = updateSRHost;
	api.updateFPSCR = &Sh4Context::UpdateFPSCR;
	api.updateIntc = aot_update_intc;
	api.biosRead = aot_bios_read;
	api.replayRead = aot_replay_read;
	api.replayWrite = aot_replay_write;
}

static void load()
{
	if (loaded)
		return;
	loaded = true;
	tableBlocks = aot_blocks;
	tableCount = aot_block_count;
	if (tableCount > 0)
		return;	// blocks linked into the executable
	if (const char *p = getenv("VNBA_GAME_MODULE_PATH"))
		modulePath = p;
	else
		modulePath = exeDir() + "/" VNBA_GAME_MODULE;
#ifdef _WIN32
	int len = MultiByteToWideChar(CP_UTF8, 0, modulePath.c_str(), -1, nullptr, 0);
	std::wstring w(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, modulePath.c_str(), -1, w.data(), len);
	HMODULE h = LoadLibraryW(w.c_str());
	if (h == nullptr)
	{
		INFO_LOG(DYNAREC, "Game module %s not found", modulePath.c_str());
		return;
	}
	moduleInit = (VnbaGameInitFn)(void *)GetProcAddress(h, VNBA_GAME_INIT_SYMBOL);
#else
	void *h = dlopen(modulePath.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (h == nullptr)
	{
		INFO_LOG(DYNAREC, "Game module %s not found", modulePath.c_str());
		return;
	}
	moduleInit = (VnbaGameInitFn)dlsym(h, VNBA_GAME_INIT_SYMBOL);
#endif
	if (moduleInit == nullptr)
	{
		ERROR_LOG(DYNAREC, "Game module %s: missing %s", modulePath.c_str(), VNBA_GAME_INIT_SYMBOL);
		return;
	}
	// first call only to learn the table (RAM pointers are passed again once the executor starts)
	aot_module_sync();
	if (tableBlocks == nullptr)
	{
		ERROR_LOG(DYNAREC, "Game module %s is incompatible with this executable: rebuild it", modulePath.c_str());
		moduleInit = nullptr;
		tableBlocks = aot_blocks;
		tableCount = 0;
	}
	else
		NOTICE_LOG(DYNAREC, "Game module %s: %zu recompiled blocks", modulePath.c_str(), tableCount);
}

void aot_module_sync()
{
	if (moduleInit == nullptr)
		return;
	AotHostApi api{};
	fillApi(api);
	size_t count = 0;
	tableBlocks = moduleInit(&api, &count);
	tableCount = tableBlocks != nullptr ? count : 0;
}

const AotBlockEntry *aot_table(size_t& count)
{
	load();
	count = tableCount;
	return tableBlocks;
}

size_t aot_table_count()
{
	load();
	return tableCount;
}

bool aot_module_loaded()
{
	load();
	return moduleInit != nullptr && tableCount > 0;
}
