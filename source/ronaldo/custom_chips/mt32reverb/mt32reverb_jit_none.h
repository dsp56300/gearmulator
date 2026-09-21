#pragma once

// The reverb back end of a host without a code generator (../jitHost.h): it has the interface
// of the others and compiles nothing, so every program runs on the interpreter.

#include "mt32reverb.h"

namespace mt32ReverbLib
{
	class Jit
	{
	public:
		static constexpr bool Available = false;

		using Run = void (*)(State*);

		bool compile(const uint8_t*, size_t) { return false; }
		Run program(size_t) const { return nullptr; }
	};
}
