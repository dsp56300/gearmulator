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

		if(m_name == "multidump")
			int d=0;
		for(uint32_t i=0; i<m_definitions.size(); ++i)
		{
			const auto& d = m_definitions[i];

			const uint8_t masked = (0xff & d.paramMask) << d.paramShift;

			if(usedMask & masked)
			{
				// next byte starts if the current mask overlaps with an existing one
				usedMask = 0;
				++byteIndex;
			}

			m_definitionToByteIndex.insert(std::make_pair(i, byteIndex));
			m_byteToDefinitionIndex.insert(std::make_pair(byteIndex, i));

			usedMask |= masked;
		}

		m_byteSize = byteIndex + 1;
	}

	bool MidiPacket::create(std::vector<uint8_t>& _dst, const std::map<MidiDataType, uint8_t>& _data) const
	{
		_dst.assign(size(), 0);

		for(size_t i=0; i<size(); ++i)
		{
			const auto range = m_byteToDefinitionIndex.equal_range(static_cast<uint32_t>(i));

			for(auto itRange = range.first; itRange != range.second; ++itRange)
			{
				const auto& d = m_definitions[itRange->second];

				switch (d.type)
				{
				case MidiDataType::Null:
					_dst[i] = 0;
					break;
				case MidiDataType::Byte:
					_dst[i] = d.byte;
					break;
				default:
					{
						const auto it = _data.find(d.type);

						if(it == _data.end())
						{
							LOG("Failed to find data of type " << static_cast<int>(d.type) << " to fill byte " << i << " of midi packet");
							return false;
						}

						_dst[i] |= (it->second & d.paramMask) << d.paramShift;
					}
				}
			}
		}
		return true;
	}

	bool MidiPacket::parse(Data& _data, ParamValues& _parameterValues, const ParameterDescriptions& _parameters, const Sysex& _src) const
	{
		if(_src.size() != size())
			return false;

		for(size_t i=0; i<_src.size(); ++i)
		{
			const auto s = _src[i];

			const auto range = m_byteToDefinitionIndex.equal_range(static_cast<uint32_t>(i));

			for(auto it = range.first; it != range.second; ++it)
			{
				const auto& d = m_definitions[it->second];

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
						uint8_t checksum = 0;

						for(uint32_t c = d.checksumFirstIndex; c <= d.checksumLastIndex; ++c)
							checksum += _src[c];

						checksum &= 0x7f;

						if(checksum != s)
						{
							LOG("Packet checksum error, calculated " << std::hex << checksum << " but data contains " << s);
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
						_parameterValues.insert(std::make_pair(std::make_pair(d.paramPart, idx), sMasked));
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
}
