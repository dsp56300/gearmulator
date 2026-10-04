#include "rackBootBlock.h"

#include "rom.h"

#include <algorithm>
#include <array>

namespace jeLib
{
	namespace
	{
		constexpr uint32_t BlockSize = 0x10000;
		constexpr uint32_t ResetAddress = 0x200;
		constexpr uint32_t InitTableAddress = 0x300;
		constexpr uint32_t RuntimeAddress = 0xa100;

		// Reset: walks the register table at 0x300 (address low byte, kind, value), then sets the stack and enters the
		// application. Kinds: 0 byte write, 1 word write, 2 read-modify-OR a byte, 3 read (clears status flags).
		constexpr uint8_t g_reset[] =
		{
			0x04, 0x80,                              // ORC #H'80,CCR
			0x7a, 0x02, 0x00, 0xff, 0xff, 0x00,      // MOV.L #H'00FFFF00,ER2
			0x7a, 0x01, 0x00, 0x00, 0x03, 0x00,      // MOV.L #H'00000300,ER1
			0x6c, 0x1a,                              // MOV.B @ER1+,R2L
			0x6c, 0x13,                              // MOV.B @ER1+,R3H
			0x6d, 0x10,                              // MOV.W @ER1+,R0
			0xa3, 0x00,                              // CMP.B #H'00,R3H
			0x47, 0x0e,                              // BEQ H'000226:8
			0xa3, 0x01,                              // CMP.B #H'01,R3H
			0x47, 0x0e,                              // BEQ H'00022A:8
			0xa3, 0x02,                              // CMP.B #H'02,R3H
			0x47, 0x0e,                              // BEQ H'00022E:8
			0xa3, 0x03,                              // CMP.B #H'03,R3H
			0x47, 0x12,                              // BEQ H'000236:8
			0x40, 0x14,                              // BRA H'00023A:8
			0x68, 0xa8,                              // MOV.B R0L,@ER2
			0x40, 0xe4,                              // BRA H'00020E:8
			0x69, 0xa0,                              // MOV.W R0,@ER2
			0x40, 0xe0,                              // BRA H'00020E:8
			0x68, 0x2b,                              // MOV.B @ER2,R3L
			0x14, 0x8b,                              // OR.B R0L,R3L
			0x68, 0xab,                              // MOV.B R3L,@ER2
			0x40, 0xd8,                              // BRA H'00020E:8
			0x68, 0x2b,                              // MOV.B @ER2,R3L
			0x40, 0xd4,                              // BRA H'00020E:8
			0x7a, 0x07, 0x00, 0x20, 0xf0, 0x00,      // MOV.L #H'0020F000,ER7
			0x5a, 0x01, 0x01, 0x00,                  // JMP @H'010100:24
		};

		enum InitKind : uint8_t { Byte, Word, Or, Read, End = 0xff };

		struct InitWrite
		{
			uint8_t address;	// 0xffffxx
			InitKind kind;
			uint16_t value;
		};

		// Bus controller, ports, timers, refresh and SCI setup the application expects to find done
		constexpr InitWrite g_init[] =
		{
			{0xf2, Byte, 0x01}, {0xec, Byte, 0xfc}, {0xed, Byte, 0xff}, {0xee, Byte, 0x00}, {0xef, Byte, 0xff},
			{0xf3, Byte, 0xfe}, {0xf5, Byte, 0x00}, {0xf6, Read, 0x00}, {0xf6, Byte, 0x00}, {0xf8, Byte, 0x00},
			{0xf9, Byte, 0x00}, {0xcb, Byte, 0x00}, {0xc9, Byte, 0x07}, {0xcd, Byte, 0x1c}, {0xd2, Byte, 0x00},
			{0xd0, Byte, 0x22}, {0xd3, Byte, 0x00}, {0xd1, Byte, 0x7f}, {0xd6, Byte, 0x00}, {0xd4, Byte, 0x00},
			{0xe9, Byte, 0x00}, {0xe8, Byte, 0x00}, {0x27, Byte, 0x00}, {0x2f, Byte, 0x00}, {0x37, Byte, 0x00},
			{0x3f, Byte, 0x00}, {0x60, Byte, 0xe0}, {0x61, Byte, 0xe0}, {0x62, Byte, 0x80}, {0x63, Byte, 0xc0},
			{0x90, Byte, 0xc0}, {0x91, Byte, 0x03}, {0x7e, Word, 0x0014}, {0x80, Word, 0x0032}, {0x78, Byte, 0x43},
			{0x62, Or, 0x04}, {0x60, Or, 0x04}, {0x94, Byte, 0xf9}, {0x98, Word, 0x07d0}, {0x92, Byte, 0x23},
			{0x60, Or, 0x10}, {0xa2, Byte, 0x00}, {0xa3, Byte, 0x00}, {0xa8, Read, 0x00}, {0xa8, Byte, 0x1f},
			{0xac, Byte, 0x60}, {0xad, Read, 0x00}, {0xad, Byte, 0x3f}, {0xb2, Byte, 0x03}, {0xb4, Read, 0x00},
			{0xb4, Byte, 0x00}, {0xb2, Byte, 0x00}, {0xb0, Byte, 0x00}, {0xb1, Byte, 0x0f}, {0xba, Byte, 0x03},
			{0xbc, Read, 0x00}, {0xbc, Byte, 0x00}, {0xba, Byte, 0x00}, {0xb8, Byte, 0x00}, {0xb9, Byte, 0x0f},
			{0x00, End, 0x0000},
		};

		// C runtime at 0xa100: signed/unsigned 32-bit division, 32-bit multiply, byte copy, function prologue/epilogue
		constexpr uint8_t g_runtime[] =
		{
			0x01, 0x00, 0x6d, 0xf2,                  // MOV.L ER2,@-ER7
			0x19, 0x22,                              // SUB.W R2,R2
			0x0f, 0x80,                              // MOV.L ER0,ER0
			0x4a, 0x06,                              // BPL H'00A110:8
			0x17, 0xb0,                              // NEG.L ER0
			0x79, 0x02, 0x01, 0x01,                  // MOV.W #H'0101,R2
			0x0f, 0x91,                              // MOV.L ER1,ER1
			0x4a, 0x04,                              // BPL H'00A118:8
			0x17, 0xb1,                              // NEG.L ER1
			0xd2, 0x01,                              // XOR.B #H'01,R2H
			0x5e, 0x00, 0xa1, 0x2e,                  // JSR @H'00A12E:24
			0x0c, 0x22,                              // MOV.B R2H,R2H
			0x47, 0x02,                              // BEQ H'00A122:8
			0x17, 0xb0,                              // NEG.L ER0
			0x0c, 0xaa,                              // MOV.B R2L,R2L
			0x47, 0x02,                              // BEQ H'00A128:8
			0x17, 0xb1,                              // NEG.L ER1
			0x01, 0x00, 0x6d, 0x72,                  // MOV.L @ER7+,ER2
			0x54, 0x70,                              // RTS
			0x01, 0x00, 0x6d, 0xf2,                  // MOV.L ER2,@-ER7
			0x01, 0x00, 0x6d, 0xf3,                  // MOV.L ER3,@-ER7
			0x0d, 0x99,                              // MOV.W E1,E1
			0x46, 0x10,                              // BNE H'00A14A:8
			0x0d, 0x82,                              // MOV.W E0,R2
			0x17, 0x72,                              // EXTU.L ER2
			0x53, 0x12,                              // DIVXU.W R1,ER2
			0x0d, 0xa8,                              // MOV.W E2,E0
			0x53, 0x10,                              // DIVXU.W R1,ER0
			0x0d, 0x81,                              // MOV.W E0,R1
			0x0d, 0x28,                              // MOV.W R2,E0
			0x40, 0x16,                              // BRA H'00A160:8
			0x0f, 0x92,                              // MOV.L ER1,ER2
			0x1a, 0x91,                              // SUB.L ER1,ER1
			0xfb, 0x20,                              // MOV.B #H'20,R3L
			0x10, 0x30,                              // SHLL.L ER0
			0x12, 0x31,                              // ROTXL.L ER1
			0x1f, 0xa1,                              // CMP.L ER2,ER1
			0x45, 0x04,                              // BCS H'00A15C:8
			0x1a, 0xa1,                              // SUB.L ER2,ER1
			0x0b, 0x00,                              // ADDS #1,ER0
			0x1a, 0x0b,                              // DEC.B R3L
			0x46, 0xf0,                              // BNE H'00A150:8
			0x01, 0x00, 0x6d, 0x73,                  // MOV.L @ER7+,ER3
			0x01, 0x00, 0x6d, 0x72,                  // MOV.L @ER7+,ER2
			0x54, 0x70,                              // RTS
			0x01, 0x00, 0x6d, 0xf2,                  // MOV.L ER2,@-ER7
			0x01, 0x00, 0x6d, 0xf3,                  // MOV.L ER3,@-ER7
			0x0f, 0x82,                              // MOV.L ER0,ER2
			0x0d, 0x93,                              // MOV.W E1,R3
			0x52, 0x23,                              // MULXU.W R2,ER3
			0x52, 0x10,                              // MULXU.W R1,ER0
			0x0d, 0xa2,                              // MOV.W E2,R2
			0x52, 0x12,                              // MULXU.W R1,ER2
			0x09, 0x38,                              // ADD.W R3,E0
			0x09, 0x28,                              // ADD.W R2,E0
			0x01, 0x00, 0x6d, 0x73,                  // MOV.L @ER7+,ER3
			0x01, 0x00, 0x6d, 0x72,                  // MOV.L @ER7+,ER2
			0x54, 0x70,                              // RTS
			0x01, 0x00, 0x6d, 0xf0,                  // MOV.L ER0,@-ER7
			0x01, 0x00, 0x6d, 0xf1,                  // MOV.L ER1,@-ER7
			0x01, 0x00, 0x6d, 0xf2,                  // MOV.L ER2,@-ER7
			0x01, 0x00, 0x6d, 0xf3,                  // MOV.L ER3,@-ER7
			0x0f, 0xa2,                              // MOV.L ER2,ER2
			0x47, 0x0a,                              // BEQ H'00A1AA:8
			0x6c, 0x0b,                              // MOV.B @ER0+,R3L
			0x68, 0x9b,                              // MOV.B R3L,@ER1
			0x0b, 0x01,                              // ADDS #1,ER1
			0x1b, 0x72,                              // DEC.L #1,ER2
			0x46, 0xf6,                              // BNE H'00A1A0:8
			0x01, 0x00, 0x6d, 0x73,                  // MOV.L @ER7+,ER3
			0x01, 0x00, 0x6d, 0x72,                  // MOV.L @ER7+,ER2
			0x01, 0x00, 0x6d, 0x71,                  // MOV.L @ER7+,ER1
			0x01, 0x00, 0x6d, 0x70,                  // MOV.L @ER7+,ER0
			0x54, 0x70,                              // RTS
			0x01, 0x00, 0x6d, 0xf5,                  // MOV.L ER5,@-ER7
			0x01, 0x00, 0x6d, 0xf4,                  // MOV.L ER4,@-ER7
			0x01, 0x00, 0x6d, 0xf3,                  // MOV.L ER3,@-ER7
			0x01, 0x00, 0x6d, 0xf2,                  // MOV.L ER2,@-ER7
			0x01, 0x00, 0x6f, 0x72, 0x00, 0x10,      // MOV.L @(H'0010:16,ER7),ER2
			0x01, 0x00, 0x6f, 0xf6, 0x00, 0x10,      // MOV.L ER6,@(H'0010:16,ER7)
			0x01, 0x00, 0x6d, 0xf2,                  // MOV.L ER2,@-ER7
			0x01, 0x00, 0x6f, 0x72, 0x00, 0x04,      // MOV.L @(H'0004:16,ER7),ER2
			0x54, 0x70,                              // RTS
			0x01, 0x00, 0x6d, 0x72,                  // MOV.L @ER7+,ER2
			0x01, 0x00, 0x6d, 0x73,                  // MOV.L @ER7+,ER3
			0x01, 0x00, 0x6d, 0x74,                  // MOV.L @ER7+,ER4
			0x01, 0x00, 0x6d, 0x75,                  // MOV.L @ER7+,ER5
			0x01, 0x00, 0x6d, 0x76,                  // MOV.L @ER7+,ER6
			0x54, 0x70,                              // RTS
		};

		struct Jump
		{
			uint32_t address;
			uint32_t target;
		};

		// The fixed addresses the application calls, each jumping into g_runtime
		constexpr std::array<Jump, 6> g_runtimeEntries =
		{{
			{0xa000, RuntimeAddress + 0x00},	// signed divide:   er0 / er1 -> er0 quotient, er1 remainder
			{0xa026, RuntimeAddress + 0x2e},	// unsigned divide
			{0xa062, RuntimeAddress + 0x6a},	// multiply:        er0 * er1 -> er0
			{0xa082, RuntimeAddress + 0x8c},	// copy er2 bytes from @er0 to @er1
			{0xa0aa, RuntimeAddress + 0xbc},	// prologue: save er2-er6
			{0xa0d2, RuntimeAddress + 0xe4},	// epilogue: restore er2-er6, return to the caller
		}};

		// Card driver entry points: nothing is ever inserted, so each fails with "not ready"
		constexpr std::array<uint16_t, 19> g_cardEntries =
		{
			0x3908, 0x3a36, 0x3a64, 0x3b86, 0x3cca, 0x4098, 0x40c6, 0x4222, 0x4264, 0x42c2,
			0x4322, 0x4386, 0x43ec, 0x4456, 0x52d6, 0x53be, 0x546e, 0x54b6, 0x8c28,
		};

		// How the application reaches a stub
		enum class Use { Call, Pointer, VectorTable };

		struct Patch
		{
			uint16_t address;
			std::vector<uint8_t> code;
			Use use;
		};

		const std::vector<Patch>& stubs()
		{
			static const std::vector<Patch> stubs =
			{
				{0x44aa, {0x79, 0x00, 0xff, 0x1c, 0x54, 0x70}, Use::Call},					// mov.w #-228,r0: no card detected
				{0x5840, {0x54, 0x70}, Use::Pointer},
				{0x5860, {0x7a, 0x00, 0x00, 0x20, 0x0d, 0x5a, 0x54, 0x70}, Use::Pointer},	// mov.l #200d5a,er0: transfer buffer
				{0x587e, {0x54, 0x70}, Use::Pointer},										// init hook of the card task
				{0x7e60, {0x19, 0x00, 0x54, 0x70}, Use::Call},								// sub.w r0,r0
				{0x8c64, {0x18, 0x88, 0x54, 0x70}, Use::Call},								// sub.b r0l,r0l: detect unchanged
				{0x8c86, {0x19, 0x00, 0x54, 0x70}, Use::Call},
				{0x235c, {0x01, 0x00, 0x6d, 0xf0, 0x5a, 0x01, 0x02, 0x04}, Use::VectorTable},	// SCI1 RXI: push er0, jmp application
				{0x236e, {0x01, 0x00, 0x6d, 0xf0, 0x5a, 0x01, 0x02, 0x00}, Use::VectorTable},	// SCI1 ERI
			};
			return stubs;
		}

		void put32(std::vector<uint8_t>& _d, const uint32_t _address, const uint32_t _value)
		{
			_d[_address] = static_cast<uint8_t>(_value >> 24);
			_d[_address + 1] = static_cast<uint8_t>(_value >> 16);
			_d[_address + 2] = static_cast<uint8_t>(_value >> 8);
			_d[_address + 3] = static_cast<uint8_t>(_value);
		}

		uint32_t get32(const std::vector<uint8_t>& _d, const size_t _offset)
		{
			return static_cast<uint32_t>(_d[_offset]) << 24 | static_cast<uint32_t>(_d[_offset + 1]) << 16 |
				static_cast<uint32_t>(_d[_offset + 2]) << 8 | _d[_offset + 3];
		}

		// A differently linked application would call other addresses; refuse it rather than boot into our stubs.
		bool isCompatible(const std::vector<uint8_t>& _firmware)
		{
			// the application's vector table: reset and the two SCI1 vectors point into the boot block
			if (get32(_firmware, 0) != 0x5a000200 || get32(_firmware, 0xd0) != 0x5a00236e || get32(_firmware, 0xd4) != 0x5a00235c)
				return false;

			// JSR/JMP @aa:24 targets and 32-bit code pointers inside the boot block
			std::vector<bool> called(BlockSize, false), pointer(BlockSize, false);
			for (size_t i = 0; i + 4 <= _firmware.size(); i += 2)
			{
				const uint32_t word = get32(_firmware, i);
				if ((word >> 24 == 0x5e || word >> 24 == 0x5a) && (word & 0xffffff) < BlockSize)
					called[word & 0xffff] = true;
				if (word < BlockSize)
					pointer[word] = true;
			}
			for (const auto& e : g_runtimeEntries)
			{
				if (!called[e.address])
					return false;
			}
			for (const auto e : g_cardEntries)
			{
				if (!called[e])
					return false;
			}
			for (const auto& p : stubs())
			{
				if ((p.use == Use::Call && !called[p.address]) || (p.use == Use::Pointer && !pointer[p.address]))
					return false;
			}
			return true;
		}
	}

	std::vector<uint8_t> createRackBootImage(const std::vector<uint8_t>& _firmware)
	{
		if (_firmware.size() != Rom::RomSizeRackFirmware || !isCompatible(_firmware))
			return {};

		std::vector<uint8_t> image(Rom::RomSizeRack, 0xff);

		// vector n is the application's jump table entry n, reset aside
		for (uint32_t v = 0; v < 64; ++v)
			put32(image, v * 4, v ? BlockSize + v * 4 : ResetAddress);
		put32(image, 52 * 4, 0x236e);
		put32(image, 53 * 4, 0x235c);

		std::copy(std::begin(g_reset), std::end(g_reset), image.begin() + ResetAddress);

		auto init = image.begin() + InitTableAddress;
		for (const auto& w : g_init)
		{
			*init++ = w.address;
			*init++ = w.kind;
			*init++ = static_cast<uint8_t>(w.value >> 8);
			*init++ = static_cast<uint8_t>(w.value);
		}

		std::copy(std::begin(g_runtime), std::end(g_runtime), image.begin() + RuntimeAddress);
		for (const auto& e : g_runtimeEntries)
			put32(image, e.address, 0x5a000000 | e.target);

		constexpr uint8_t notReady[] = {0x7a, 0x00, 0xff, 0xff, 0xff, 0x2b, 0x54, 0x70};	// mov.l #-213,er0; rts
		for (const auto e : g_cardEntries)
			std::copy(std::begin(notReady), std::end(notReady), image.begin() + e);
		for (const auto& p : stubs())
			std::copy(p.code.begin(), p.code.end(), image.begin() + p.address);

		std::copy(_firmware.begin(), _firmware.end(), image.begin() + BlockSize);
		return image;
	}
}
