#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pluginLib
{
	enum class MidiDataType
	{
		Null,
		Byte,
		DeviceId,
		Checksum,
		Bank,
		Program,
		Parameter,
		ParameterIndex,
		ParameterValue,
		Page,
		Part
	};

	class MidiPacket
	{
	public:
		struct MidiByte
		{
			MidiDataType type = MidiDataType::Null;
			uint8_t byte = 0;
			std::string name;
			uint32_t checksumFirstIndex = 0;
			uint32_t checksumLastIndex = 0;
			uint32_t checksumInitValue = 0;
		};

		std::vector<MidiByte> bytes;
	};
}
