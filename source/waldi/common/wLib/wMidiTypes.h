#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace wLib
{
	enum MidiHeaderByte : uint8_t
	{
		IdWaldorf = 0x3e,
		IdDeviceOmni = 0x7f
	};

	enum SysexIndex
	{
		IdxSysexBegin = 0,
		IdxIdWaldorf = 1,
		IdxIdMachine = 2,
		IdxDeviceId = 3,
		IdxCommand = 4,

		// dumps / dump requests
		IdxBuffer = 5,
		IdxLocation = 6,
	};

	// The positions and values that tell a dump of _machine: the command, and buffer and location if given. Any
	// device ID, a device answers with its own or the omni one
	inline std::vector<std::pair<uint32_t, uint8_t>> getDumpHeader(const uint8_t _machine, const uint8_t _command,
		const std::optional<uint8_t> _buffer = {}, const std::optional<uint8_t> _location = {})
	{
		std::vector<std::pair<uint32_t, uint8_t>> bytes{{IdxIdWaldorf, IdWaldorf}, {IdxIdMachine, _machine}, {IdxCommand, _command}};

		if (_buffer)
			bytes.emplace_back(IdxBuffer, *_buffer);
		if (_location)
			bytes.emplace_back(IdxLocation, *_location);

		return bytes;
	}
}
