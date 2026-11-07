#pragma once
#include "types.h"
#include <string>
// BIOS-free boot (see aot_boot.cpp)
bool aot_boot_snapshot_available();
bool aot_boot_use_fallback();
bool aot_boot_pending();
void aot_boot_rearm();
bool aot_boot_hook(u32 pc);
bool aot_boot_bios_free();
bool aot_bios_window(u32 addr);
// boot snapshot of a game (romset name) for a NAOMI BIOS region (config::Region)
std::string aot_boot_snapshot_path(const std::string& game, int region);
