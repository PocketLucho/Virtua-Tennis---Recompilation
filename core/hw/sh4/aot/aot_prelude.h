// Prelude included by every generated AOT translation unit.
#pragma once
#include "types.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/dyna/ngen.h"
#include "hw/sh4/dyna/ssa.h"
#include <cstring>
#define AOT_NO_CANON_REG
#define SHIL_MODE 1
#include "hw/sh4/dyna/shil_canonical.h"
#include "aot_runtime.h"

// Register views by byte offset inside Sh4Context (generated code is built with -fno-strict-aliasing)
#define R32(o) (*(u32 *)((u8 *)ctx + (o)))
#define F32(o) (*(f32 *)((u8 *)ctx + (o)))
#define R64(o) (*(u64 *)((u8 *)ctx + (o)))

struct AnyPtr {
	void *p;
	template<typename T> operator T *() const { return (T *)p; }
};
static inline f32 as_f32(u32 v) { f32 f; memcpy(&f, &v, 4); return f; }
