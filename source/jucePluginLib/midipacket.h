#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "types.h"

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
		static constexpr uint32_t InvalidIndex = 0xffffffff;

		struct MidiDataDefinition
		{
			MidiDataType type = MidiDataType::Null;

			uint8_t byte = 0;

			std::string paramName;
			uint8_t paramMask = 0xff;
			uint8_t paramShiftRight = 0;	// right shift for unpacking from midi, left for packing
			uint8_t paramShiftLeft = 0;		// left shift for unpacking from midi, right for packing
			uint8_t paramPart = AnyPart;

			uint32_t checksumFirstIndex = 0;
			uint32_t checksumLastIndex = 0;
			uint8_t checksumInitValue = 0;

			uint8_t packValue(const ParamValue _unmasked) const
			{
				 return static_cast<uint8_t>(((_unmasked & paramMask) << paramShiftRight) >> paramShiftLeft);
			}

			ParamValue unpackValue(const uint8_t _masked) const
			{
				return ((_masked << paramShiftLeft) >> paramShiftRight) & paramMask;
			}

			bool doMasksOverlap(const MidiDataDefinition& _d) const
			{
				return (packValue(0xff) & _d.packValue(0xff)) != 0;
			}
		};

		using Data = std::map<MidiDataType, uint8_t>;
		using ParamIndex = std::pair<uint8_t,uint32_t>;

		struct ParamIndexHash
		{
		    std::size_t operator () (const ParamIndex& p) const
			{
				static_assert(sizeof(std::size_t) > sizeof(uint32_t) + sizeof(uint8_t), "need a 64 bit compiler");
				return (static_cast<std::size_t>(p.first) << 32) | p.second;
		    }
		};

		using ParamIndices = std::set<ParamIndex>;
		using ParamValues = std::unordered_map<ParamIndex, ParamValue, ParamIndexHash>;	// part, index => value
		using AnyPartParamValues = std::vector<std::optional<ParamValue>>;				// index => value
		using NamedParamValues = std::map<std::pair<uint8_t,std::string>, ParamValue>;	// part, name => value
		using Sysex = std::vector<uint8_t>;

		MidiPacket() = default;
		explicit MidiPacket(std::string _name, std::vector<MidiDataDefinition>&& _bytes);

		const std::vector<MidiDataDefinition>& definitions() { return m_definitions; }
		uint32_t size() const { return m_byteSize; }

		bool create(std::vector<uint8_t>& _dst, const Data& _data, const NamedParamValues& _paramValues) const;
		bool create(std::vector<uint8_t>& _dst, const Data& _data) const;
		bool parse(Data& _data, AnyPartParamValues& _parameterValues, const ParameterDescriptions& _parameters, const Sysex& _src, bool _ignoreChecksumErrors = true) const;
		bool parse(Data& _data, ParamValues& _parameterValues, const ParameterDescriptions& _parameters, const Sysex& _src, bool _ignoreChecksumErrors = true) const;
		bool parse(Data& _data, const std::function<void(ParamIndex, ParamValue)>& _addParamValueCallback, const ParameterDescriptions& _parameters, const Sysex& _src, bool _ignoreChecksumErrors = true) const;
		bool getParameterIndices(ParamIndices& _indices, const ParameterDescriptions& _parameters) const;
		bool getDefinitionsForByteIndex(std::vector<const MidiDataDefinition*>& _result, uint32_t _byteIndex) const;
		bool getParameterIndicesForByteIndex(std::vector<ParamIndex>& _result, const ParameterDescriptions& _parameters, uint32_t _byteIndex) const;

		uint32_t getByteIndexForType(MidiDataType _type) const;
		uint32_t getByteIndexForParameterName(const std::string& _name) const;
		std::vector<uint32_t> getDefinitionIndicesForParameterName(const std::string& _name) const;
		const MidiDataDefinition *getDefinitionByParameterName(const std::string& _name) const;

		ParamValue getParameterValue(const Sysex& _sysex, const std::vector<uint32_t>& _definitionIndices) const;

		bool updateChecksums(Sysex& _data) const;

		bool hasPartDependentParameters() const { return m_numDifferentPartsUsedInParameters; }

	private:

		static uint8_t calcChecksum(const MidiDataDefinition& _d, const Sysex& _src);

		const std::string m_name;
		std::vector<MidiDataDefinition> m_definitions;
		std::map<uint32_t, uint32_t> m_definitionToByteIndex;
		std::vector<std::vector<uint32_t>> m_byteToDefinitionIndex;
		uint32_t m_byteSize = 0;
		bool m_hasParameters = false;
		uint32_t m_numDifferentPartsUsedInParameters = 0;
	};
}
