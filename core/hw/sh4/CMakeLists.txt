target_sources(${PROJECT_NAME} PRIVATE
        dyna/aot_emit.cpp
        aot/aot_executor.cpp
        aot/aot_boot.cpp
        aot/aot_static.cpp
        aot/aot_game.cpp
        dyna/blockmanager.cpp
        dyna/blockmanager.h
        dyna/decoder.cpp
        dyna/decoder.h
        dyna/decoder_opcodes.h
        dyna/driver.cpp
        dyna/ngen.h
        dyna/shil_canonical.h
        dyna/shil.cpp
        dyna/shil.h
        dyna/ssa.cpp
        dyna/ssa.h
        dyna/ssa_regalloc.h
        fsca-table.h
        interpr/sh4_fpu.cpp
        interpr/sh4_interpreter.cpp
        interpr/sh4_opcodes.cpp
        interpr/sh4_opcodes.h
        modules/bsc.cpp
        modules/ccn.cpp
        modules/ccn.h
        modules/cpg.cpp
        modules/dmac.cpp
        modules/dmac.h
        modules/fastmmu.cpp
        modules/intc.cpp
        modules/mmu.cpp
        modules/mmu.h
        modules/modules.h
        modules/rtc.cpp
        modules/serial.cpp
        modules/tmu.cpp
        modules/ubc.cpp
        modules/wince.h
        sh4_cache.h
        sh4_cycles.cpp
        sh4_cycles.h
        sh4_core.h
        sh4_core_regs.cpp
        sh4_if.h
        sh4_interpreter.h
        sh4_interrupts.cpp
        sh4_interrupts.h
        sh4_mem.cpp
        sh4_mem.h
        sh4_mmr.cpp
        sh4_mmr.h
        sh4_opcode_list.cpp
        sh4_opcode_list.h
        sh4_rom.cpp
        sh4_rom.h
        sh4_sched.cpp
        sh4_sched.h
        storeq.cpp)

set(VNBA_AOT_DIR "" CACHE PATH "Directory with generated AOT translation units")
if(VNBA_AOT_DIR)
	file(GLOB VNBA_AOT_SOURCES "${VNBA_AOT_DIR}/aot_gen_*.cpp")
	if(MSVC)
		set_source_files_properties(${VNBA_AOT_SOURCES} PROPERTIES COMPILE_OPTIONS "/O2;/w;/bigobj")
	else()
		set_source_files_properties(${VNBA_AOT_SOURCES} PROPERTIES COMPILE_OPTIONS "-fno-strict-aliasing;-O2;-w")
	endif()
	target_sources(${PROJECT_NAME} PRIVATE ${VNBA_AOT_SOURCES})
else()
	target_sources(${PROJECT_NAME} PRIVATE aot/aot_table_stub.cpp)
endif()
target_sources(${PROJECT_NAME} PRIVATE aot/aot_module.cpp aot/aot_dll_api.h)
