#pragma once

// The LSP back end of a host without a code generator (../jitHost.h): it has the interface of
// the others and never becomes ready, so the dispatcher stays on the interpreter.

#include "lsp_program.h"

namespace lspLib
{
	class LSPJIT
	{
	public:
		static constexpr bool Available = false;

		LSPJIT(const LSPProgram&, LSPRuntime&) {}

		bool ready() const { return false; }
		bool compile(const LSPInstr*) { return false; }
		void runProgram() {}
	};
}
