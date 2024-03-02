#include "midipacket.h"

#include <cassert>

#include "parameterdescriptions.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace pluginLib
{
	MidiPacket::MidiPacket(std::string _name, std::vector<MidiDataDefinition>&& _bytes) : m_name(std::move(_name)), m_definitions(std::move(_bytes))
	{
		uint8_t usedMask = 0;
		uint32_t byteIndex = 0;

		m_byteToDefinitionIndex.reserve(m_definitions.size());

		std::set<uint32_t> usedParts;

		for(uint32_t i=0; i<m_definitions.size(); ++i)
		{
			const auto& d = m_definitions[i];

			if(d.paramPart != AnyPart)
				usedParts.insert(d.paramPart);

			if(d.type == MidiDataType::Parameter)
				m_hasParameters = true;

			const auto masked = static_cast<uint8_t>((0xff & d.paramMask) << d.paramShift);

			if(usedMask & masked)
			{
				// next byte starts if the current mask overlaps with an existing one
				usedMask = 0;
				++byteIndex;
			}

			m_definitionToByteIndex.insert(std::make_pair(i, byteIndex));

			if(byteIndex >= m_byteToDefinitionIndex.size())
				m_byteToDefinitionIndex.emplace_back();

			m_byteToDefinitionIndex[byteIndex].push_back(i);

			usedMask |= masked;
		}

		m_byteSize = byteIndex + 1;

		m_numDifferentPartsUsedInParameters = static_cast<uint32_t>(usedParts.size());
	}

	bool MidiPacket::create(std::vector<uint8_t>& _dst, const Data& _data, const NamedParamValues& _paramValues) const
	{
		_dst.assign(size(), 0);

		std::map<uint32_t, uint32_t> pendingChecksums;	// byte index => description index

		for(size_t i=0; i<size(); ++i)
		{
			if(i >= m_byteToDefinitionIndex.size())
				continue;

			const auto& range = m_byteToDefinitionIndex[i];

			for(auto itRange = range.begin(); itRange != range.end(); ++itRange)
			{
				const auto& d = m_definitions[*itRange];

				switch (d.type)
				{
				case MidiDataType::Null:
					_dst[i] = 0;
					break;
				case MidiDataType::Byte:
					_dst[i] = d.byte;
					break;
				case MidiDataType::Parameter:
					{
						const auto it = _paramValues.find(std::make_pair(d.paramPart, d.paramName));
						if(it == _paramValues.end())
						{
							LOG("Failed to find value for parameter " << d.paramName << ", part " << d.paramPart);
							return false;
						}
						_dst[i] |= d.getMaskedValue(it->second);
					}
					break;
				case MidiDataType::Checksum:
					pendingChecksums.insert(std::make_pair(static_cast<uint32_t>(i), *itRange));
					break;
				default:
					{
						const auto it = _data.find(d.type);

						if(it == _data.end())
						{
							LOG("Failed to find data of type " << static_cast<int>(d.type) << " to fill byte " << i << " of midi packet");
							return false;
						}

						_dst[i] = it->second;
					}
				}
			}
		}

		for (const auto& pendingChecksum : pendingChecksums)
		{
			const auto byteIndex = pendingChecksum.first;
			const auto descIndex = pendingChecksum.second;

			_dst[byteIndex] = calcChecksum(m_definitions[descIndex], _dst);
		}

		return true;
	}

	bool MidiPacket::create(std::vector<uint8_t>& _dst, const Data& _data) const
	{
		return create(_dst, _data, {});
	}

	bool MidiPacket::parse(Data& _data, AnyPartParamValues& _parameterValues, const ParameterDescriptions& _parameters, const Sysex& _src, bool _ignoreChecksumErrors) const
	{
		if(m_numDifferentPartsUsedInParameters > 0)
		{
			LOG("Failed to parse midi packet " << m_name << " with parameters for " << m_numDifferentPartsUsedInParameters << " different parts into parameter values that are not part-aware");
			return false;
		}

		_parameterValues.reserve(_src.size());

		return parse(_data, [&](ParamIndex _paramIndex, uint8_t _value)
		{
			const auto idx = _paramIndex.second;
			if(_parameterValues.size() <= idx)
				_parameterValues.resize(idx + 1);
			_parameterValues[idx] = _value;
		}, _parameters, _src, _ignoreChecksumErrors);
	}

	bool MidiPacket::parse(Data& _data, ParamValues& _parameterValues, const ParameterDescriptions& _parameters, const Sysex& _src, bool _ignoreChecksumErrors/* = true*/) const
	{
		_parameterValues.reserve(_src.size() << 1);

		return parse(_data, [&](ParamIndex _paramIndex, uint8_t _value)
		{
			_parameterValues.insert(std::make_pair(_paramIndex, _value));
		}, _parameters, _src, _ignoreChecksumErrors);
	}
	
	bool MidiPacket::parse(Data& _data, const std::function<void(ParamIndex, uint8_t)>& _addParamValueCallback, const ParameterDescriptions& _parameters, const Sysex& _src, bool _ignoreChecksumErrors) const
	{
		if(_src.size() != size())
			return false;

		for(size_t i=0; i<_src.size(); ++i)
		{
			const auto s = _src[i];

			if(i >= m_byteToDefinitionIndex.size())
				continue;

			const auto& range = m_byteToDefinitionIndex[i];

			for(auto it = range.begin(); it != range.end(); ++it)
			{
				const auto& d = m_definitions[*it];

				switch (d.type)
				{
				case MidiDataType::Null: 
					continue;
				case MidiDataType::Byte:
					if(d.byte != s)
						return false;
					break;
				case MidiDataType::Checksum:
					{
						const uint8_t checksum = calcChecksum(d, _src);

						if(checksum != s)
						{
							LOG("Packet checksum error, calculated " << std::hex << static_cast<int>(checksum) << " but data contains " << static_cast<int>(s));
							if(!_ignoreChecksumErrors)
								return false;
						}
					}
					continue;
				case MidiDataType::DeviceId:
				case MidiDataType::Bank:
				case MidiDataType::Program:
				case MidiDataType::ParameterIndex:
				case MidiDataType::ParameterValue:
				case MidiDataType::Page:
				case MidiDataType::Part:
					_data.insert(std::make_pair(d.type, s));
					break;
				case MidiDataType::Parameter:
					{
						uint32_t idx;
						if(!_parameters.getIndexByName(idx, d.paramName))
						{
							LOG("Failed to find named parameter " << d.paramName << " while parsing midi packet, midi byte " << i);
							return false;
						}
						const auto sMasked = (s >> d.paramShift) & d.paramMask;
						_addParamValueCallback(std::make_pair(d.paramPart, idx), static_cast<uint8_t>(sMasked));
					}
					break;
				default:
					assert(false && "unknown data type");
					return false;
				}
			}
		}
		return true;
	}

	bool MidiPacket::getParameterIndices(ParamIndices& _indices, const ParameterDescriptions& _parameters) const
	{
		if(!m_hasParameters)
			return true;

		for (const auto & d : m_definitions)
		{
			if(d.type != MidiDataType::Parameter)
				continue;

			uint32_t index;
			if(!_parameters.getIndexByName(index, d.paramName))
			{
				LOG("Failed to retrieve index for parameter " << d.paramName);
				return false;
			}

			_indices.insert(std::make_pair(d.paramPart, index));
		}
		return true;
	}

	bool MidiPacket::getDefinitionsForByteIndex(std::vector<const MidiDataDefinition*>& _result, uint32_t _byteIndex) const
	{
		if (_byteIndex >= m_byteToDefinitionIndex.size())
			return false;

		const auto& defs = m_byteToDefinitionIndex[_byteIndex];

		for (const auto idx : defs)
		{
			const auto &d = m_definitions[idx];
			_result.emplace_back(&d);
		}
		return true;
	}

	bool MidiPacket::getParameterIndicesForByteIndex(std::vector<ParamIndex>& _result, const ParameterDescriptions& _parameters, const uint32_t _byteIndex) const
	{
		if (_byteIndex >= m_byteToDefinitionIndex.size())
			return false;

		const auto& defs = m_byteToDefinitionIndex[_byteIndex];

		for (const auto idx : defs)
		{
			const auto &d = m_definitions[idx];

			uint32_t paramIdx;

			if(!_parameters.getIndexByName(paramIdx, d.paramName))
			{
				LOG("Failed to retrieve index for parameter " << d.paramName);
				return false;
			}

			_result.emplace_back(d.paramPart, paramIdx);
		}
		return true;
	}

	uint32_t MidiPacket::getByteIndexForType(MidiDataType _type) const
	{
		for(uint32_t i=0; i<m_definitions.size(); ++i)
		{
			const auto& d = m_definitions[i];
			if(d.type != _type)
				continue;

			return m_definitionToByteIndex.find(i)->second;
		}
		return InvalidIndex;
	}

	uint32_t MidiPacket::getByteIndexForParameterName(const std::string& _name) const
	{
		for(uint32_t i=0; i<m_definitions.size(); ++i)
		{
			const auto& d = m_definitions[i];

			if(d.type != MidiDataType::Parameter)
				continue;

			if(d.paramName != _name)
				continue;

			return m_definitionToByteIndex.find(i)->second;
		}
		return InvalidIndex;
	}

	const MidiPacket::MidiDataDefinition* MidiPacket::getDefinitionByParameterName(const std::string& _name) const
	{
		for (const auto& definition : m_definitions)
		{
			if (definition.paramName == _name)
				return &definition;
		}

		return nullptr;
	}

	bool MidiPacket::updateChecksums(Sysex& _data) const
	{
		if (_data.size() != m_byteSize)
			return false;

		bool res = false;

		for (uint32_t i=0; i<m_definitions.size(); ++i)
		{
			const auto& def = m_definitions[i];

			if (def.type != MidiDataType::Checksum)
				continue;

			const auto byteIndex = m_definitionToByteIndex.find(i)->second;

			const auto c = calcChecksum(def, _data);
			_data[byteIndex] = c;
			res = true;
		}

		return res;
	}

	uint8_t MidiPacket::calcChecksum(const MidiDataDefinition& _d, const Sysex& _src)
	{
		auto checksum = _d.checksumInitValue;

		for(uint32_t c = _d.checksumFirstIndex; c <= _d.checksumLastIndex; ++c)
			checksum += _src[c];

		return checksum & 0x7f;
	}
}
