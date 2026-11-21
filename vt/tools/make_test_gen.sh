#!/bin/bash
# Writes a tiny synthetic builder/gen (no game code) so CI can test the module toolchain.
#   vt/tools/make_test_gen.sh <package dir>
set -e
G="$1/builder/gen"; mkdir -p "$G"
cat > "$G/aot_gen_00.cpp" <<'X'
#include "hw/sh4/aot/aot_prelude.h"
void b_0C021000_000001_00000000(Sh4Context *ctx)
{
	ctx->cycle_counter -= 2;
	R32(192) = aot_read32(R32(196));
	aot_write32(R32(200), R32(192));
	ctx->pc = ctx->jdyn;
}
X
cat > "$G/aot_gen_table.cpp" <<'X'
#include "hw/sh4/aot/aot_runtime.h"
void b_0C021000_000001_00000000(Sh4Context *);
const AotBlockEntry aot_blocks[] = {
	{ 0x0C021000u, 0x000001u, 0x00000000u, 4u, b_0C021000_000001_00000000 },
};
const size_t aot_block_count = sizeof(aot_blocks) / sizeof(aot_blocks[0]);
X
