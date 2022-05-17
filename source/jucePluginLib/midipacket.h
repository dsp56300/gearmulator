#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pluginLib
{
	class ParameterDescriptions;

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
		static constexpr uint8_t AnyPart = 0xff;

		struct MidiDataDefinition
		{
			MidiDataType type = MidiDataType::Null;

			uint8_t byte = 0;

			std::string paramName;
			uint8_t paramMask = 0xff;
			uint8_t paramShift = 0;
			uint8_t paramPart = AnyPart;

			uint32_t checksumFirstIndex = 0;
			uint32_t checksumLastIndex = 0;
			uint32_t checksumInitValue = 0;
		};

		using Data = std::map<MidiDataType, uint8_t>;
		using ParamValues = std::map<std::pair<uint8_t,uint32_t>, uint8_t>;	// part, index => value
		using Sysex = const std::vector<uint8_t>;

		MidiPacket() = default;
		explicit MidiPacket(std::string _name, std::vector<MidiDataDefinition>&& _bytes);

		const std::vector<MidiDataDefinition>& definitions() { return m_definitions; }
		uint32_t size() const { return m_byteSize; }

		bool create(std::vector<uint8_t>& _dst, const std::map<MidiDataType, uint8_t>& _data) const;
		bool parse(Data& _data, ParamValues& _parameterValues, const ParameterDescriptions& _parameters, Sysex& _src) const;

	private:
		const std::string m_name;
		std::vector<MidiDataDefinition> m_definitions;
		std::map<uint32_t, uint32_t> m_definitionToByteIndex;
		std::multimap<uint32_t, uint32_t> m_byteToDefinitionIndex;
		uint32_t m_byteSize = 0;
	};
}
