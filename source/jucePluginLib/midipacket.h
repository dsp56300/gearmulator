#pragma once

#include <cstdint>
#include <map>
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

		MidiPacket() = default;
		explicit MidiPacket(std::vector<MidiByte>&& bytes) : m_bytes(std::move(bytes)) {}

		const std::vector<MidiByte>& bytes() { return m_bytes; }
		size_t size() const { return m_bytes.size(); }

		bool create(std::vector<uint8_t>& _dst, std::map<MidiDataType, uint8_t>& _data) const;

	private:
		std::vector<MidiByte> m_bytes;
	};
}
