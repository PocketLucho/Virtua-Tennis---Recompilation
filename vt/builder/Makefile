# Builds vt_game.dll (the recompiled game code) from the sources generated from YOUR ROM.
# Used by build_game.ps1; needs llvm-mingw (downloaded automatically by the script).
CXX = x86_64-w64-mingw32-clang++
CXXFLAGS = -std=c++17 -O2 -fno-strict-aliasing -w -DNDEBUG -Isdk/core -Isdk/core/deps -Isdk/core/deps/nowide/include
SRCS = $(sort $(wildcard gen/aot_gen_*.cpp)) sdk/core/hw/sh4/aot/aot_dll_shim.cpp
OBJS = $(addprefix obj/,$(notdir $(SRCS:.cpp=.o)))
OUT = ../vt_game.dll

vpath %.cpp gen sdk/core/hw/sh4/aot

all: $(OUT)

obj/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT): $(OBJS)
	$(CXX) -shared -static -o $@ $(OBJS) -Wl,--exclude-all-symbols -s
