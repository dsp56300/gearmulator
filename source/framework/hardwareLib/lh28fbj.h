#pragma once

#include <cstdint>
#include <vector>

namespace hwLib
{
	// Sharp LH28F160BJ / LH28F320BJ class 16 / 32 Mbit boot block flash on a
	// 16-bit bus, the Intel command set: read array (FF), read identifier
	// (90), read status (70), clear status (50), word program (40 or 10 then
	// the data), block erase (20 then D0 at the block), suspend / resume
	// (B0 / D0) and the block lock commands (60 then 01 / D0).  Operations
	// complete at once and the status register reports ready, with the
	// sequence error bits set when a setup command is not followed by what it
	// expects.
	//
	// The array is the caller's byte vector with words stored big-endian,
	// the way a big-endian host sees them.  Bottom boot: eight 8 KB
	// parameter blocks, then blocks of 64 KB.
	class Lh28fBj
	{
	public:
		static constexpr uint32_t Size16M = 0x200000;
		static constexpr uint32_t Size32M = 0x400000;
		static constexpr uint32_t ParameterBlockSize = 0x2000;
		static constexpr uint32_t ParameterBlocks = 8;
		static constexpr uint32_t MainBlockSize = 0x10000;
		static constexpr uint16_t ManufacturerSharp = 0x00b0;
		static constexpr uint16_t Device160Bottom = 0x00e9;
		static constexpr uint16_t Device320Bottom = 0x00b5;

		static constexpr uint8_t StatusReady = 0x80;
		static constexpr uint8_t StatusEraseError = 0x20;
		static constexpr uint8_t StatusProgramError = 0x10;
		static constexpr uint8_t StatusSequenceError = StatusEraseError | StatusProgramError;

		// _size is a power of two; the array grows to it, erased, when shorter.
		Lh28fBj(std::vector<uint8_t>& _array, uint32_t _size, uint16_t _device, uint16_t _manufacturer = ManufacturerSharp);
		void reset();

		uint32_t size() const { return m_size; }
		bool isReadArray() const { return m_mode == Mode::ReadArray; }
		// word access at a byte address; the array is read straight in array mode
		uint16_t read(uint32_t _addr) const;
		void write(uint32_t _addr, uint16_t _data);

		uint8_t status() const { return m_status; }
		uint64_t programs() const { return m_programs; }
		uint64_t erases() const { return m_erases; }
		// set by program and erase, for whoever persists the array
		bool isDirty() const { return m_dirty; }
		void clearDirty() { m_dirty = false; }

		uint32_t blockStart(uint32_t _addr) const;
		uint32_t blockSize(uint32_t _addr) const;

	private:
		enum class Mode
		{
			ReadArray,
			ReadId,
			ReadStatus,
			ProgramSetup,
			EraseSetup,
			LockSetup
		};

		uint16_t arrayWord(uint32_t _addr) const;

		std::vector<uint8_t>& m_array;
		const uint32_t m_size;
		const uint16_t m_manufacturer;
		const uint16_t m_device;
		Mode m_mode = Mode::ReadArray;
		uint8_t m_status = StatusReady;
		uint64_t m_programs = 0;
		uint64_t m_erases = 0;
		bool m_dirty = false;
	};
}
