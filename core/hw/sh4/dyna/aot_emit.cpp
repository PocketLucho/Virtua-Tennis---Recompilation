// AOT C++ emitter: converts decoded+optimized SHIL blocks into C++ source.
// Enabled by setting the environment variable VNBA_AOT_DUMP=<output file>.
// Every block compiled by the dynarec is also written as a C++ function.
#include "types.h"
#include "ngen.h"
#include "blockmanager.h"
#include "shil.h"
#include "aot_canon_reg.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/sh4_mem.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <mutex>
#include <tuple>

std::map<const void *, std::string> &aotCanonNames()
{
	static std::map<const void *, std::string> names;
	return names;
}

namespace {

std::string hex(u32 v)
{
	char b[16];
	snprintf(b, sizeof(b), "0x%08Xu", v);
	return b;
}

// Expression for a register viewed as u32 / f32 / u64, using its byte offset in Sh4Context.
std::string R32(const shil_param &p) { return "R32(" + std::to_string(p.reg_offset()) + ")"; }
std::string F32(const shil_param &p) { return "F32(" + std::to_string(p.reg_offset()) + ")"; }
std::string R64(const shil_param &p) { return "R64(" + std::to_string(p.reg_offset()) + ")"; }

std::string u32Expr(const shil_param &p)
{
	if (p.is_imm())
		return hex(p._imm);
	return R32(p);
}

class AotEmitter : public Sh4Dynarec
{
public:
	std::ostringstream out;
	bool failed = false;
	std::string failReason;

	void init(Sh4Context&, Sh4CodeBuffer&) override {}
	void compile(RuntimeBlockInfo*, bool, bool) override {}
	void mainloop(void*) override {}
	void handleException(host_context_t&) override {}
	bool rewrite(host_context_t&, void*) override { return false; }

	void canonStart(const shil_opcode *op) override {
		args.clear();
		rets.clear();
		callee.clear();
	}
	void canonParam(const shil_opcode *op, const shil_param *prm, CanonicalParamType tp) override
	{
		switch (tp)
		{
		case CPT_u32:
			args.push_back(u32Expr(*prm));
			break;
		case CPT_f32:
			if (prm->is_imm())
				args.push_back("as_f32(" + hex(prm->_imm) + ")");
			else
				args.push_back(F32(*prm));
			break;
		case CPT_ptr:
			if (!prm->is_reg()) { fail("ptr param is not a register"); return; }
			args.push_back("AnyPtr{(u8 *)ctx + " + std::to_string(prm->reg_offset()) + "}");
			break;
		case CPT_sh4ctx:
			args.push_back("ctx");
			break;
		case CPT_u32rv:
		case CPT_f32rv:
		case CPT_u64rvL:
		case CPT_u64rvH:
			rets.push_back({tp, prm});
			break;
		}
	}
	void canonCall(const shil_opcode *op, void *function) override
	{
		auto it = aotCanonNames().find(function);
		if (it == aotCanonNames().end()) { fail("unknown canonical function"); return; }
		callee = it->second;
	}
	void canonFinish(const shil_opcode *op) override
	{
		if (failed)
			return;
		// arguments were pushed in reverse order
		std::string call = callee + "(";
		for (size_t i = args.size(); i-- > 0;)
		{
			call += args[i];
			if (i != 0)
				call += ", ";
		}
		call += ")";
		if (rets.empty())
			out << "\t" << call << ";\n";
		else if (rets.size() == 1 && rets[0].first == CPT_u32rv)
			out << "\t" << u32Lvalue(*rets[0].second) << " = " << call << ";\n";
		else if (rets.size() == 1 && rets[0].first == CPT_f32rv)
			out << "\t" << F32(*rets[0].second) << " = " << call << ";\n";
		else if (rets.size() == 2 && rets[0].first == CPT_u64rvL && rets[1].first == CPT_u64rvH)
			out << "\t{ u64 t_ = " << call << "; " << R32(*rets[0].second) << " = (u32)t_; "
				<< R32(*rets[1].second) << " = (u32)(t_ >> 32); }\n";
		else
			fail("unsupported return combination");
	}

	void fail(const std::string& why)
	{
		if (!failed)
			failReason = why;
		failed = true;
	}

	std::string u32Lvalue(const shil_param &p)
	{
		if (!p.is_reg()) { fail("lvalue not a register"); return "DUMMY"; }
		return R32(p);
	}

	void emitOp(RuntimeBlockInfo *block, shil_opcode &op)
	{
		switch (op.op)
		{
		case shop_mov32:
			out << "\t" << u32Lvalue(op.rd) << " = " << u32Expr(op.rs1) << ";\n";
			break;
		case shop_mov64:
			if (!op.rd.is_reg() || !op.rs1.is_reg()) { fail("mov64 operands"); return; }
			out << "\t" << R64(op.rd) << " = " << R64(op.rs1) << ";\n";
			break;
		case shop_jdyn:
		case shop_jcond:
			out << "\t" << u32Lvalue(op.rd) << " = " << u32Expr(op.rs1);
			if (op.rs2.is_imm())
				out << " + " << hex(op.rs2._imm);
			else if (!op.rs2.is_null())
				out << " + " << u32Expr(op.rs2);
			out << ";\n";
			break;
		case shop_ifb:
			if (op.rs1._imm)
				out << "\tctx->pc = " << hex(op.rs2._imm) << ";\n";
			out << "\tOpPtr[" << hex(op.rs3._imm) << "](ctx, " << hex(op.rs3._imm) << ");\n";
			break;
		case shop_readm:
		{
			std::string addr = u32Expr(op.rs1);
			if (op.rs3.is_imm())
				addr += " + " + hex(op.rs3._imm);
			else if (op.rs3.is_reg())
				addr += " + " + R32(op.rs3);
			switch (op.size)
			{
			case 1: out << "\t" << u32Lvalue(op.rd) << " = (u32)aot_read8sx(" << addr << ");\n"; break;
			case 2: out << "\t" << u32Lvalue(op.rd) << " = (u32)aot_read16sx(" << addr << ");\n"; break;
			case 4: out << "\t" << u32Lvalue(op.rd) << " = aot_read32(" << addr << ");\n"; break;
			case 8:
				if (!op.rd.is_reg()) { fail("read64 dst"); return; }
				out << "\t" << R64(op.rd) << " = aot_read64(" << addr << ");\n";
				break;
			default: fail("readm size");
			}
			break;
		}
		case shop_writem:
		{
			std::string addr = u32Expr(op.rs1);
			if (op.rs3.is_imm())
				addr += " + " + hex(op.rs3._imm);
			else if (op.rs3.is_reg())
				addr += " + " + R32(op.rs3);
			switch (op.size)
			{
			case 1: out << "\taot_write8(" << addr << ", (u8)" << u32Expr(op.rs2) << ");\n"; break;
			case 2: out << "\taot_write16(" << addr << ", (u16)" << u32Expr(op.rs2) << ");\n"; break;
			case 4: out << "\taot_write32(" << addr << ", " << u32Expr(op.rs2) << ");\n"; break;
			case 8:
				if (!op.rs2.is_reg()) { fail("write64 src"); return; }
				out << "\taot_write64(" << addr << ", " << R64(op.rs2) << ");\n";
				break;
			default: fail("writem size");
			}
			break;
		}
		default:
			// everything else has a portable canonical implementation
			shil_chf[op.op](&op);
			break;
		}
	}

	std::string emitBlock(RuntimeBlockInfo *block, const std::string &fname)
	{
		out.str("");
		failed = false;
		out << "// " << hex(block->vaddr) << " fpscr " << hex(block->fpu_cfg.full)
			<< " ops " << block->oplist.size() << " sh4 " << block->sh4_code_size << " bytes\n";
		out << "static void " << fname << "(Sh4Context *ctx)\n{\n";
		out << "\tctx->cycle_counter -= " << block->guest_cycles << ";\n";
		Sh4Dynarec *saved = sh4Dynarec;
		sh4Dynarec = this;
		for (shil_opcode &op : block->oplist)
		{
			emitOp(block, op);
			if (failed)
				break;
		}
		sh4Dynarec = saved;
		switch (block->BlockType)
		{
		case BET_StaticJump:
		case BET_StaticCall:
			out << "\tctx->pc = " << hex(block->BranchBlock) << ";\n";
			break;
		case BET_Cond_0:
		case BET_Cond_1:
			out << "\tctx->pc = (" << (block->has_jcond ? "ctx->jdyn" : "ctx->sr.T") << " == "
				<< (block->BlockType & 1) << ") ? " << hex(block->BranchBlock) << " : " << hex(block->NextBlock) << ";\n";
			break;
		case BET_DynamicJump:
		case BET_DynamicCall:
		case BET_DynamicRet:
			out << "\tctx->pc = ctx->jdyn;\n";
			break;
		case BET_DynamicIntr:
			out << "\tctx->pc = ctx->jdyn;\n\taot_update_intc();\n";
			break;
		case BET_StaticIntr:
			out << "\tctx->pc = " << hex(block->NextBlock) << ";\n\taot_update_intc();\n";
			break;
		default:
			fail("block end type");
		}
		out << "}\n\n";
		return out.str();
	}

private:
	std::vector<std::string> args;
	std::vector<std::pair<CanonicalParamType, const shil_param *>> rets;
	std::string callee;
};

AotEmitter emitter;
FILE *dumpFile;
std::set<std::tuple<u32, u32, u32>> dumped;
std::mutex dumpMutex;
bool dumpChecked;

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

} // namespace

void aot_dump_close()
{
	if (dumpFile != nullptr)
	{
		fclose(dumpFile);
		dumpFile = nullptr;
	}
}

bool aot_dump_enabled()
{
	// also on when running AOT code, so that fallback blocks are decoded identically
	extern bool aot_executor_enabled();
	static const bool enabled = getenv("VNBA_AOT_DUMP") != nullptr || aot_executor_enabled();
	return enabled;
}

// Called by the dynarec driver for every block about to be compiled.
bool aot_static_generating;	// set by aot_static_generate()

void aot_dump_block(RuntimeBlockInfo *block)
{
	// builder mode: only the static decoder's blocks (not the BIOS boot executed before it)
	static const bool generateMode = getenv("VNBA_GENERATE") != nullptr;
	if (generateMode && !aot_static_generating)
		return;
	std::lock_guard<std::mutex> lock(dumpMutex);
	if (!dumpChecked)
	{
		dumpChecked = true;
		const char *path = getenv("VNBA_AOT_DUMP");
		if (path != nullptr)
		{
			dumpFile = fopen(path, "a");
			INFO_LOG(DYNAREC, "AOT dump to %s: %s", path, dumpFile ? "ok" : "failed");
		}
	}
	if (dumpFile == nullptr)
		return;
	u32 fpu = block->fpu_cfg.full & 0x00180003; // FR is resolved at runtime; PR, SZ, RM shape decoding
	u32 hash = fnv1a(block->addr, block->sh4_code_size);
	if (!dumped.insert({block->vaddr, fpu, hash}).second)
		return;
	char fname[64];
	snprintf(fname, sizeof(fname), "b_%08X_%06X_%08X", block->vaddr, fpu, hash);
	std::string code = emitter.emitBlock(block, fname);
	if (emitter.failed)
	{
		fprintf(dumpFile, "//! FAILED %08X %06X: %s\n", block->vaddr, fpu, emitter.failReason.c_str());
	}
	else
	{
		fprintf(dumpFile, "//@BLOCK %08X %06X %08X %u %s\n", block->vaddr, fpu, hash, block->sh4_code_size, fname);
		fputs(code.c_str(), dumpFile);
	}
	fflush(dumpFile);
}
