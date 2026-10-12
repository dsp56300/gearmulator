#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emu88Lib::gs
{
	using Message = std::vector<uint8_t>;

	constexpr uint8_t g_rolandId = 0x41;
	constexpr uint8_t g_deviceId = 0x10;
	constexpr uint8_t g_modelId = 0x42;
	constexpr uint8_t g_cmdRq1 = 0x11;
	constexpr uint8_t g_cmdDt1 = 0x12;

	constexpr uint8_t g_partsPerGroup = 16;

	// The GS addresses the part editor reads and writes. A part parameter is 40 1x yy (or 40 4x yy), with x the
	// part's address block; partAddress() builds one.
	constexpr uint32_t g_masterVolume = 0x400004;
	constexpr uint32_t g_gsReset = 0x40007f;
	// SC-88 and later: double (32 parts) or single module mode. The unit reinitializes.
	constexpr uint32_t g_systemModeSet = 0x00007f;

	enum class PartParam : uint8_t
	{
		ToneNumber = 0x00,	// two bytes: bank select MSB, program
		Program = 0x01,		// the tone number's second byte
		RxChannel = 0x02,	// 00-0F, 10 = off
		RxNoteMessage = 0x08,	// what the front panel's MUTE turns off
		UseForRhythm = 0x15,	// 0 off, 1 drum map 1, 2 drum map 2
		Level = 0x19,
		Pan = 0x1c,			// 0 random, 1-127 L63-R63
		RxBankSelect = 0x23,	// the SC-55mkII and later; GM System On clears it
		KeepTone = 0x25,	// the SC-55st, the SC-88 and later: a program change for nothing keeps the tone
	};

	// 40 4x yy, the SC-88 and later
	enum class PartMapParam : uint8_t
	{
		ToneMap = 0x00,		// the map a part plays from, what bank select LSB (CC#32) sets; 0 = the map-0 one
		ToneMap0 = 0x01,	// the map a part on map 0 uses
	};

	// Roland checksum over address and data
	uint8_t checksum(const uint8_t* _data, size_t _size);

	Message dt1(uint32_t _address, const std::vector<uint8_t>& _data);
	Message dt1(uint32_t _address, uint8_t _value);
	// Request Data 1. The Sound Canvases answer a request that starts at a parameter and covers exactly its size;
	// the SC-8850 also takes ranges.
	Message rq1(uint32_t _address, uint32_t _size);

	// A part's address block within its group, 0-15 = part 1-16: part 10 is block 0, parts 1-9 blocks 1-9, parts
	// 11-16 A-F
	uint8_t partBlock(uint8_t _part);
	// The part of an address block, the inverse of partBlock
	uint8_t blockPart(uint8_t _block);

	// 40 1x yy for a part of a group, addressed on the group's own port, where 40 means that port's group
	uint32_t partAddress(uint8_t _part, PartParam _param);
	uint32_t partAddress(uint8_t _part, PartMapParam _param);

	// The size a parameter has in an RQ1 or a DT1
	uint8_t paramSize(PartParam _param);

	// Adds _offset to a GS address, which counts in seven bit bytes
	uint32_t addressAdd(uint32_t _address, uint32_t _offset);

	struct Dt1
	{
		uint32_t address = 0;
		const uint8_t* data = nullptr;
		size_t size = 0;
	};

	// A complete GS DT1 (any device ID) with a valid checksum. A message that is anything else returns false.
	bool parseDt1(const uint8_t* _sysex, size_t _size, Dt1& _result);
}
