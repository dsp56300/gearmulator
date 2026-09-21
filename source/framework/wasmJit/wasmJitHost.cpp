#include "wasmJitHost.h"

#include <emscripten.h>

#include <vector>

// The ticket is how the host reports back without the embedder having to export anything: the
// JavaScript side writes the outcome into it, whenever that is, and the C++ side polls. It is
// two int32 - state (0 pending, 1 ready, 2 failed) and the index in the indirect function
// table - and has to outlive its compile even when its owner does not: see release().

namespace
{
	EM_JS_DEPS(chipsWasmJitDeps, "$addFunction,$removeFunction,$UTF8ToString");

	// clang-format off
	EM_JS(void, chipsWasmJitCompile, (const uint8_t* _module, int _size, const char* _signature, int32_t* _ticket),
	{
		_module >>>= 0;
		_ticket >>>= 0;
		var signature = UTF8ToString(_signature);
		// A copy: the module is compiled after the caller's buffer is gone.
		var bytes = HEAPU8.slice(_module, _module + _size);
		var imports = { env: { memory: wasmMemory } };

		// HEAP32 is looked up when the result is written, not now: the memory may have grown.
		var finish = function(instance)
		{
			try
			{
				HEAP32[(_ticket >> 2) + 1] = addFunction(instance.exports.f, signature);
				HEAP32[_ticket >> 2] = 1;
			}
			catch(e)
			{
				err('wasm jit: cannot install the function (link with -sALLOW_TABLE_GROWTH): ' + e);
				HEAP32[_ticket >> 2] = 2;
			}
		};
		var fail = function(e)
		{
			err('wasm jit: compile failed: ' + e);
			HEAP32[_ticket >> 2] = 2;
		};

		// The main thread of a browser refuses synchronous compiles beyond a few KB.
		if(typeof window === 'undefined')
		{
			try
			{
				finish(new WebAssembly.Instance(new WebAssembly.Module(bytes), imports));
				return;
			}
			catch(e)
			{
				// Fall through to the asynchronous path, which also reports a genuine error.
			}
		}
		WebAssembly.instantiate(bytes, imports).then(function(result) { finish(result.instance); }, fail);
	});

	EM_JS(void, chipsWasmJitRemove, (int _function),
	{
		try { removeFunction(_function); } catch(e) {}
	});
	// clang-format on

	// Tickets whose owner went away while the host was still compiling.
	std::vector<int32_t*>& abandoned()
	{
		static std::vector<int32_t*> tickets;
		return tickets;
	}

	void reapAbandoned()
	{
		auto& tickets = abandoned();
		for(size_t i = 0; i < tickets.size();)
		{
			auto* ticket = tickets[i];
			if(ticket[0] == 0)
			{
				++i;
				continue;
			}
			if(ticket[0] == 1)
				chipsWasmJitRemove(ticket[1]);
			delete[] ticket;
			tickets[i] = tickets.back();
			tickets.pop_back();
		}
	}
}

namespace wasmJit
{
	void HostFunction::submit(const uint8_t* _module, const size_t _size, const char* _signature)
	{
		release();
		reapAbandoned();

		m_ticket = new int32_t[2]{0, 0};
		m_state = State::Pending;
		chipsWasmJitCompile(_module, static_cast<int>(_size), _signature, m_ticket);
		poll();
	}

	HostFunction::State HostFunction::poll()
	{
		if(m_state != State::Pending)
			return m_state;
		const auto* raw = m_ticket;
		if(raw[0] == 0)
			return m_state;
		if(raw[0] == 1)
		{
			m_pointer = reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(raw[1])));
			m_state = State::Ready;
		}
		else
		{
			m_state = State::Failed;
		}
		return m_state;
	}

	void HostFunction::release()
	{
		if(m_ticket)
		{
			auto* raw = m_ticket;
			if(raw[0] == 0)
			{
				abandoned().push_back(raw);	// the host still owes it an answer
			}
			else
			{
				if(raw[0] == 1)
					chipsWasmJitRemove(raw[1]);
				delete[] raw;
			}
		}
		m_ticket = nullptr;
		m_pointer = nullptr;
		m_state = State::Empty;
	}
} // namespace wasmJit
