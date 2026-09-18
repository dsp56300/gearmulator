#pragma once

#include <array>
#include <cstdint>

// The Boss reverb gate array (HG61H20R36F / BOS-007) of the MT-32 and CM-32L: a fixed-sequence
// delay machine, not a CPU. Its microcode ROM holds 256-byte programs, one per mode/level/time
// combination, and a program is 64 steps of 4 bytes: a 14-bit RAM offset (bytes 0 and 2, relative
// to a rotating position), a control byte per half-step (bytes 1 and 3) and one saw-mask bit per
// half-step (byte 2 bits 7 and 6). Everything a step does is decided by those bytes, so a program
// is straight-line code once the ROM is known; the JIT backends emit it as such.

namespace mt32ReverbLib
{
	static constexpr unsigned RamSize      = 0x4000;
	static constexpr unsigned RamMask      = RamSize - 1;
	static constexpr unsigned ProgramBytes = 0x100;
	static constexpr unsigned StepBytes    = 4;
	static constexpr unsigned StepCount    = ProgramBytes / StepBytes;

	// The step before which the accumulator is latched as the right / left output.
	static constexpr unsigned OutRightStep = 0xbc / StepBytes;
	static constexpr unsigned OutLeftStep  = 0xe0 / StepBytes;
	// Steps below this write the right input, the rest the left one.
	static constexpr unsigned InputSplit   = 0x80 / StepBytes;

	// Everything the chip carries between frames plus the per-frame I/O. Both engines read and
	// write it in the same layout, so they can be swapped on any frame boundary; the JIT reaches
	// the fields by offsetof, hence the plain int32 registers.
	struct State
	{
		int32_t position = 0;		// 0..RamMask
		int32_t accumulator = 0;	// sign-extended 16-bit
		int32_t shifter = 0;		// sign-extended 16-bit
		int32_t carry = 0;			// 0 or 1
		int32_t sawBits = 0;		// 1 << (time & 3), tested against each half-step's mask
		int32_t inputLeft = 0;		// 16-bit input samples
		int32_t inputRight = 0;
		int32_t outLeft = 0;		// 16-bit wet samples
		int32_t outRight = 0;
		// Last: the scalars stay within the short offset range of the JIT's loads and stores.
		std::array<int16_t, RamSize> ram{};

		void reset()
		{
			ram.fill(0);
			position = accumulator = shifter = carry = 0;
			outLeft = outRight = 0;
		}
	};

	// The four ROM bytes of one step, decoded.
	struct Step
	{
		unsigned offset;		// RAM offset, already masked
		uint8_t  control[2];	// the two half-step control bytes
		uint8_t  mask[2];		// the saw masks of the two half-steps

		bool write()    const { return (control[0] & 2) == 0; }	// store input or accumulator to RAM
		bool useInput() const { return (control[0] & 1) != 0; }	// the stored value is the input
		bool load()     const { return (control[0] & 4) == 0; }	// the shifter takes the stored / read value
		static bool clear(const uint8_t _control)  { return (_control & 0x10) == 0; }
		static bool negate(const uint8_t _control) { return (_control & 8) != 0; }
	};

	inline Step decodeStep(const uint8_t* _program, const unsigned _step)
	{
		const uint8_t* b = _program + _step * StepBytes;
		Step s;
		s.offset = (b[0] | (unsigned(b[2]) << 8)) & RamMask;
		s.control[0] = b[1];
		s.control[1] = b[3];
		s.mask[0] = static_cast<uint8_t>(((b[1] >> 4) & 0x0e) | (b[2] >> 7));
		s.mask[1] = static_cast<uint8_t>(((b[3] >> 4) & 0x0e) | ((b[2] >> 6) & 1));
		return s;
	}
}
