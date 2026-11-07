#pragma once
// Maps canonical SHIL implementation function pointers to their C++ names so the
// AOT C++ emitter can call them by name from generated translation units.
#include <map>
#include <string>
std::map<const void *, std::string> &aotCanonNames();
struct AotCanonReg {
	AotCanonReg(const void *fn, const char *op, const char *sub) {
		aotCanonNames()[fn] = std::string(op) + "::" + sub + "::impl";
	}
};
