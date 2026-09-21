#pragma once

// A WebAssembly binary emitter, as small as the chip JITs need it.
//
// It produces one kind of module: a single function, exported as "f", over a linear memory that
// is imported as env.memory - the memory of the program that generated it, so the code works on
// the emulation state in place, through absolute addresses. That is the whole interface of a
// chip back end to its host (wasmJitHost.h): bytes in, a function pointer out.
//
// The emitter is plain C++ with no dependency on a WebAssembly host, so a back end can be built
// and its output inspected anywhere. Only the MVP instruction set is used - no sign-extension
// operators, no multi-value, no bulk memory - to run wherever WebAssembly runs.
//
// Control flow is structured, as in the format: block() / loop() / ifThen() open a construct
// and return its Label, end() closes the innermost one, and br() / brIf() take a Label and work
// out the relative depth. Branching to a block's label leaves the block, to a loop's label
// restarts it.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace wasmJit
{
	enum class ValType : uint8_t
	{
		I32 = 0x7f,
		I64 = 0x7e,
	};

	using Bytes = std::vector<uint8_t>;

	inline void writeU32(Bytes& _out, uint32_t _value)
	{
		do
		{
			uint8_t byte = _value & 0x7f;
			_value >>= 7;
			if (_value)
				byte |= 0x80;
			_out.push_back(byte);
		} while (_value);
	}

	inline void writeS64(Bytes& _out, int64_t _value)
	{
		for (;;)
		{
			const uint8_t byte = static_cast<uint8_t>(_value) & 0x7f;
			_value >>= 7;	// arithmetic
			const bool done = (_value == 0 && !(byte & 0x40)) || (_value == -1 && (byte & 0x40));
			_out.push_back(done ? byte : static_cast<uint8_t>(byte | 0x80));
			if (done)
				return;
		}
	}

	struct Local
	{
		uint32_t index = 0;
	};

	struct Label
	{
		uint32_t id = 0;
	};

	// The body of the function. Operands follow the format's stack discipline: push, push, op.
	class Function
	{
	public:
		// `_params` become locals 0..n-1.
		explicit Function(std::initializer_list<ValType> _params = {}) : m_params(_params) {}

		const std::vector<ValType>& params() const { return m_params; }
		const std::vector<ValType>& locals() const { return m_locals; }
		const Bytes& code() const
		{
			assert(m_open.empty() && "unclosed block");
			return m_code;
		}

		Local param(const uint32_t _index) const
		{
			assert(_index < m_params.size());
			return {_index};
		}

		Local local(const ValType _type)
		{
			m_locals.push_back(_type);
			return {static_cast<uint32_t>(m_params.size() + m_locals.size() - 1)};
		}

		// ---- locals and constants
		void get(const Local _l) { op(0x20); u32(_l.index); }
		void set(const Local _l) { op(0x21); u32(_l.index); }
		void tee(const Local _l) { op(0x22); u32(_l.index); }
		void i32Const(const int32_t _v) { op(0x41); writeS64(m_code, _v); }
		void i64Const(const int64_t _v) { op(0x42); writeS64(m_code, _v); }
		// An absolute address in the imported memory.
		void address(const uintptr_t _address) { i32Const(static_cast<int32_t>(static_cast<uint32_t>(_address))); }
		void address(const void* _pointer) { address(reinterpret_cast<uintptr_t>(_pointer)); }

		// ---- memory: [address] -> [value] and [address, value] -> []
		void i32Load(const uint32_t _offset = 0) { mem(0x28, 2, _offset); }
		void i64Load32S(const uint32_t _offset = 0) { mem(0x34, 2, _offset); }
		void i32Load8U(const uint32_t _offset = 0) { mem(0x2d, 0, _offset); }
		void i32Load16U(const uint32_t _offset = 0) { mem(0x2f, 1, _offset); }
		void i32Store(const uint32_t _offset = 0) { mem(0x36, 2, _offset); }
		void i64Store32(const uint32_t _offset = 0) { mem(0x3e, 2, _offset); }

		// ---- i32
		void i32Eqz() { op(0x45); }
		void i32LeS() { op(0x4c); }
		void i32Add() { op(0x6a); }
		void i32Sub() { op(0x6b); }
		void i32And() { op(0x71); }
		void i32Shl() { op(0x74); }

		// ---- i64
		void i64Eqz() { op(0x50); }
		void i64LtS() { op(0x53); }
		void i64LeS() { op(0x57); }
		void i64GeS() { op(0x59); }
		void i64Add() { op(0x7c); }
		void i64Sub() { op(0x7d); }
		void i64Mul() { op(0x7e); }
		void i64And() { op(0x83); }
		void i64Shl() { op(0x86); }
		void i64ShrS() { op(0x87); }
		void i64ShrU() { op(0x88); }

		// ---- conversions
		void i32WrapI64() { op(0xa7); }
		void i64ExtendI32S() { op(0xac); }
		void i64ExtendI32U() { op(0xad); }
		// int64 -> the int64 holding its low 32 bits sign-extended (MVP form of i64.extend32_s).
		void i64SignExtend32()
		{
			i32WrapI64();
			i64ExtendI32S();
		}

		// [a, b, condition(i32)] -> [condition ? a : b]
		void select() { op(0x1b); }
		void drop() { op(0x1a); }

		// ---- control
		Label block() { return open(0x02); }
		Label loop() { return open(0x03); }
		// [condition(i32)] -> []; runs to the matching end() when the condition is non-zero.
		Label ifThen() { return open(0x04); }

		void end()
		{
			assert(!m_open.empty());
			m_open.pop_back();
			op(0x0b);
		}

		void br(const Label _label) { op(0x0c); u32(depth(_label)); }
		void brIf(const Label _label) { op(0x0d); u32(depth(_label)); }

		// [index(i32)] -> []; an index past the table takes `_default`.
		void brTable(const std::vector<Label>& _labels, const Label _default)
		{
			op(0x0e);
			u32(static_cast<uint32_t>(_labels.size()));
			for (const auto label : _labels)
				u32(depth(label));
			u32(depth(_default));
		}

		void ret() { op(0x0f); }

	private:
		void op(const uint8_t _op) { m_code.push_back(_op); }
		void u32(const uint32_t _v) { writeU32(m_code, _v); }

		void mem(const uint8_t _op, const uint32_t _alignLog2, const uint32_t _offset)
		{
			op(_op);
			u32(_alignLog2);
			u32(_offset);
		}

		Label open(const uint8_t _op)
		{
			op(_op);
			op(0x40);	// no result
			m_open.push_back(m_nextLabel);
			return {m_nextLabel++};
		}

		uint32_t depth(const Label _label) const
		{
			for (size_t i = m_open.size(); i-- > 0;)
				if (m_open[i] == _label.id)
					return static_cast<uint32_t>(m_open.size() - 1 - i);
			assert(false && "branch to a label that is not open");
			return 0;
		}

		std::vector<ValType> m_params;
		std::vector<ValType> m_locals;
		Bytes m_code;
		std::vector<uint32_t> m_open;
		uint32_t m_nextLabel = 0;
	};

	// The module around a function: type, memory import, function, export "f", code.
	inline Bytes buildModule(const Function& _function)
	{
		const auto section = [](Bytes& _out, const uint8_t _id, const Bytes& _content)
		{
			_out.push_back(_id);
			writeU32(_out, static_cast<uint32_t>(_content.size()));
			_out.insert(_out.end(), _content.begin(), _content.end());
		};
		const auto name = [](Bytes& _out, const std::string& _name)
		{
			writeU32(_out, static_cast<uint32_t>(_name.size()));
			_out.insert(_out.end(), _name.begin(), _name.end());
		};

		Bytes module = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

		Bytes types;
		writeU32(types, 1);
		types.push_back(0x60);
		writeU32(types, static_cast<uint32_t>(_function.params().size()));
		for (const auto type : _function.params())
			types.push_back(static_cast<uint8_t>(type));
		writeU32(types, 0);	// no results
		section(module, 1, types);

		Bytes imports;
		writeU32(imports, 1);
		name(imports, "env");
		name(imports, "memory");
		imports.push_back(0x02);	// memory
		imports.push_back(0x00);	// no maximum: whatever the host's memory is
		writeU32(imports, 0);		// at least 0 pages
		section(module, 2, imports);

		Bytes functions;
		writeU32(functions, 1);
		writeU32(functions, 0);	// type 0
		section(module, 3, functions);

		Bytes exports;
		writeU32(exports, 1);
		name(exports, "f");
		exports.push_back(0x00);	// function
		writeU32(exports, 0);
		section(module, 7, exports);

		// Locals are declared as runs of one type.
		Bytes body;
		std::vector<std::pair<uint32_t, ValType>> runs;
		for (const auto type : _function.locals())
		{
			if (!runs.empty() && runs.back().second == type)
				++runs.back().first;
			else
				runs.emplace_back(1, type);
		}
		writeU32(body, static_cast<uint32_t>(runs.size()));
		for (const auto& run : runs)
		{
			writeU32(body, run.first);
			body.push_back(static_cast<uint8_t>(run.second));
		}
		body.insert(body.end(), _function.code().begin(), _function.code().end());
		body.push_back(0x0b);	// end of function

		Bytes code;
		writeU32(code, 1);
		writeU32(code, static_cast<uint32_t>(body.size()));
		code.insert(code.end(), body.begin(), body.end());
		section(module, 10, code);

		return module;
	}
} // namespace wasmJit
