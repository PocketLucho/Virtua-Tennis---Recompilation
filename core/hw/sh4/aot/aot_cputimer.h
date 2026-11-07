#pragma once
// Accumulates host time spent inside the SH-4 executor (VNBA_CPU_TIME=1 prints it at exit)
#include <chrono>
#include <cstdio>
#include <cstdlib>
struct VnbaCpuTimer {
	std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	static double& total() { static double t; return t; }
	static double& sys() { static double t; return t; }
	static unsigned long long& fallbacks() { static unsigned long long n; return n; }
	static unsigned long long& blockTsc() { static unsigned long long n; return n; }
	static void report() { fprintf(stderr, "SH4 executor host time: %.2f s (system events %.2f s, fallback blocks %llu, in blocks %.2f Gcycles)\n", total(), sys(), fallbacks(), blockTsc() / 1e9); }
	~VnbaCpuTimer() {
		static bool reg = getenv("VNBA_CPU_TIME") && !atexit(report);
		(void)reg;
		total() += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	}
};
