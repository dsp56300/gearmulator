#pragma once

// The host side of the WebAssembly chip JITs: turns a module built with wasmEmitter.h into a
// function the C++ side can call through an ordinary function pointer.
//
// Only a JavaScript embedder can do that, so this exists under Emscripten only. The module is
// instantiated over the program's own memory and its export "f" is put into the program's
// indirect function table; a call is then a wasm-to-wasm call_indirect, with no JavaScript in
// between. The final link needs -sALLOW_TABLE_GROWTH for the table to take new entries; without
// it the compile fails, which a chip treats like any other failed compile and stays on its
// interpreter.
//
// Compilation is asynchronous where it has to be. A browser's main thread may not compile
// synchronously, so there the function arrives some event-loop turns after submit() and the
// owner polls for it - the same handshake the native back ends have with their compile thread,
// without a thread. Where a synchronous compile is allowed (Node, workers) the function is
// ready when submit() returns, which makes tests deterministic.

#include <cstddef>
#include <cstdint>

namespace wasmJit
{
	class HostFunction
	{
	public:
		enum class State : uint8_t
		{
			Empty,		// nothing submitted
			Pending,	// the host is compiling
			Ready,		// pointer() is callable
			Failed,
		};

		HostFunction() = default;
		~HostFunction() { release(); }
		HostFunction(const HostFunction&) = delete;
		HostFunction& operator=(const HostFunction&) = delete;

		// Hands a module to the host, replacing whatever this held. `_signature` is the
		// Emscripten signature string of export "f" ("v" for void(), "vi" for void(int32)).
		void submit(const uint8_t* _module, size_t _size, const char* _signature);

		// Cheap; call it until it stops returning Pending.
		State poll();

		// The function as a pointer to cast to its real type, or nullptr unless Ready.
		void* pointer() const { return m_pointer; }

		// Drops the function. A compile still in flight is abandoned, not waited for.
		void release();

	private:
		// {state, table index}, written by the host. Raw storage that can outlive this object.
		int32_t* m_ticket = nullptr;
		void* m_pointer = nullptr;
		State m_state = State::Empty;
	};
} // namespace wasmJit
