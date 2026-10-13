#pragma once

#include "88lib/deviceModel.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The MT-32 dialect: Roland SysEx with model ID 16H, which the LA boards (MT-32, CM-32L, CM-32LN) and the CM-32P
// share. The two kinds of board split the address space between them - the LA board 02-20 and 7F, the PCM board 50-52
// and 7F - so the CM-64, which is one of each on one MIDI IN, takes the same stream on both.
//
// What the firmware does with a message was read back from the emulated boards: the LA boards with data requests,
// the CM-32P, which answers none, from its RAM. Where the two firmware generations of the LA board differ, "old" is
// the MT-32 1.x program, "new" the MT-32 2.x and CM-32L one.
namespace emu88Lib::mt32
{
	using Message = std::vector<uint8_t>;

	constexpr uint8_t g_rolandId = 0x41;
	constexpr uint8_t g_modelId = 0x16;
	// Unit #17, what the boards answer to unless their front panel sets another unit number. The parts' temporary
	// timbres are also reachable by MIDI channel, with the channel (00-0F) as the device ID; see g_channelTimbreTemp.
	constexpr uint8_t g_unitDeviceId = 0x10;
	constexpr uint8_t g_cmdRq1 = 0x11;
	constexpr uint8_t g_cmdDt1 = 0x12;

	// The largest DT1 the LA boards answer a request with; a longer area comes as several.
	constexpr size_t g_maxDt1Size = 256;

	enum class Board : uint8_t { La, Pcm };

	// The LA firmware generations, by what they do with a message
	enum class LaFirmware : uint8_t
	{
		Old,	// MT-32 1.x
		New,	// MT-32 2.x, CM-32L, CM-32LN
	};

	bool hasLaBoard(DeviceModel _model);
	bool hasPcmBoard(DeviceModel _model);
	// Whether the model speaks this dialect at all: hasLaBoard() || hasPcmBoard()
	bool isMt32Model(DeviceModel _model);
	LaFirmware laFirmware(DeviceModel _model);
	// Whether the board answers data requests: the LA boards do, the CM-32P does not
	bool answersDataRequests(Board _board);

	// ---- the address map, in 7 bit addresses

	// LA board. Per part means parts 1-8; the rhythm part (part 9) has a patch temp but no timbre temp.
	constexpr uint32_t g_channelTimbreTemp = 0x020000;	// the timbre temp of the part on the device ID's channel
	constexpr uint32_t g_patchTemp = 0x030000;			// 9 x 16 bytes, part 9 = rhythm at 03 01 00
	constexpr uint32_t g_rhythmSetup = 0x030110;		// 4 bytes per key from key 24
	constexpr uint32_t g_timbreTemp = 0x040000;			// 8 x 246 bytes
	constexpr uint32_t g_patchMemory = 0x050000;		// 128 x 8 bytes
	constexpr uint32_t g_timbreMemory = 0x080000;		// 64 x 256 bytes, 246 of them used
	constexpr uint32_t g_laSystem = 0x100000;			// 23 bytes
	constexpr uint32_t g_display = 0x200000;			// MT-32: text for the display, not memory
	// PCM board
	constexpr uint32_t g_pcmPatchTemp = 0x500000;		// 6 x 21 bytes
	constexpr uint32_t g_pcmPatchMemory = 0x510000;		// 128 x 19 bytes
	constexpr uint32_t g_pcmSystem = 0x520000;			// 17 bytes
	// Both: any address 7F xx xx resets all parameters
	constexpr uint32_t g_allParametersReset = 0x7f0000;

	constexpr uint8_t g_laParts = 9;			// eight melodic parts and the rhythm part
	constexpr uint8_t g_laTimbreParts = 8;
	constexpr uint8_t g_rhythmPart = 8;
	constexpr uint8_t g_pcmParts = 6;
	constexpr uint8_t g_firstRhythmKey = 24;

	constexpr size_t g_patchTempSize = 0x10;
	constexpr size_t g_rhythmKeySize = 4;
	constexpr size_t g_timbreCommonSize = 0x0e;
	constexpr size_t g_partialSize = 0x3a;
	constexpr size_t g_partials = 4;
	constexpr size_t g_timbreSize = g_timbreCommonSize + g_partials * g_partialSize;	// 246
	constexpr size_t g_timbreMemoryStride = 0x100;
	constexpr size_t g_timbreMemoryCount = 64;
	constexpr size_t g_patchMemorySize = 8;
	constexpr size_t g_patchMemoryCount = 128;
	constexpr size_t g_laSystemSize = 0x17;
	constexpr size_t g_pcmPatchTempSize = 0x15;
	constexpr size_t g_pcmPatchMemorySize = 0x13;
	constexpr size_t g_pcmPatchMemoryCount = 128;
	constexpr size_t g_pcmSystemSize = 0x11;

	// The rhythm keys the board keeps a setup for: 24-87 on the old firmware, 24-108 on the new one
	uint8_t rhythmKeyCount(LaFirmware _firmware);

	// The memory areas, each a run of equal entries
	enum class Area : uint8_t
	{
		PatchTemp,
		RhythmSetup,
		TimbreTemp,
		PatchMemory,
		TimbreMemory,
		LaSystem,
		PcmPatchTemp,
		PcmPatchMemory,
		PcmSystem,
		Count
	};

	struct AreaInfo
	{
		Area area;
		Board board;
		const char* name;
		uint32_t address;		// 7 bit
		uint16_t entrySize;		// the bytes an entry holds
		uint16_t entryStride;	// in linear bytes; the timbre memory spaces its 246 byte entries 256 apart
		uint16_t count;			// for the rhythm setup the new firmware's count; see rhythmKeyCount()
	};
	const AreaInfo& areaInfo(Area _area);
	// The entries an area has on a board with that firmware
	uint16_t entryCount(Area _area, LaFirmware _firmware);

	// A byte of the address space
	struct Location
	{
		Area area = Area::Count;
		uint16_t entry = 0;
		uint16_t offset = 0;
		bool valid() const { return area != Area::Count; }
	};
	// Where an address (7 bit) is; invalid for one that holds no memory, like the gaps behind the timbre memory's
	// entries. The channel addressed timbre temp is not in it, the board resolves that by the parts' channels.
	Location locate(Board _board, uint32_t _address, LaFirmware _firmware);
	uint32_t address(const Location& _location);

	// ---- messages

	// 7 bit address <-> linear
	uint32_t toLinear(uint32_t _address);
	uint32_t fromLinear(uint32_t _linear);
	uint32_t addressAdd(uint32_t _address, uint32_t _offset);

	Message dt1(uint32_t _address, const uint8_t* _data, size_t _size, uint8_t _deviceId = g_unitDeviceId);
	Message dt1(uint32_t _address, const std::vector<uint8_t>& _data, uint8_t _deviceId = g_unitDeviceId);
	// The same as several DT1 of at most g_maxDt1Size data bytes, the way the boards answer a long request
	std::vector<Message> dt1Chunks(uint32_t _address, const uint8_t* _data, size_t _size,
		uint8_t _deviceId = g_unitDeviceId);
	// Request Data 1. The LA boards answer anything that starts in memory, across areas, in g_maxDt1Size chunks.
	Message rq1(uint32_t _address, uint32_t _size, uint8_t _deviceId = g_unitDeviceId);

	struct Dt1
	{
		uint8_t deviceId = 0;
		uint32_t address = 0;
		const uint8_t* data = nullptr;
		size_t size = 0;
	};
	// A complete DT1 of model 16H with a valid checksum. Anything else returns false.
	bool parseDt1(const uint8_t* _sysex, size_t _size, Dt1& _result);
}
