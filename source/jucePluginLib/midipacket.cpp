#include "midipacket.h"

#include <cassert>

#include "parameterdescriptions.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace pluginLib
{
	bool MidiPacket::create(std::vector<uint8_t>& _dst, const std::map<MidiDataType, uint8_t>& _data) const
	{
		_dst.reserve(size());

		for(size_t i=0; i<size(); ++i)
		{
			const auto& b = m_bytes[i];
			switch (b.type)
			{
			case MidiDataType::Null:
				_dst.push_back(0);
				break;
			case MidiDataType::Byte:
				_dst.push_back(b.byte);
				break;
			default:
				{
					const auto it = _data.find(b.type);

					if(it == _data.end())
					{
						LOG("Failed to find data of type " << static_cast<int>(b.type) << " to fill byte " << i << " of midi packet");
						return false;
					}

					_dst.push_back(it->second);
				}
			}
		}
		return true;
	}

	bool MidiPacket::parse(std::map<MidiDataType, uint8_t>& _data, std::map<uint32_t, uint8_t>& _parameterValues, const ParameterDescriptions& _parameters, const std::vector<uint8_t>& _src) const
	{
		if(_src.size() != size())
			return false;

		for(size_t i=0; i<_src.size(); ++i)
		{
			const auto& b = m_bytes[i];
			const auto s = _src[i];

			switch (b.type)
			{
			case MidiDataType::Null: 
				continue;
			case MidiDataType::Byte:
				if(b.byte != s)
					return false;
				break;
			case MidiDataType::Checksum:
				{
					uint8_t checksum = 0;

					for(uint32_t c = b.checksumFirstIndex; c <= b.checksumLastIndex; ++c)
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
				_data.insert(std::make_pair(b.type, s));
				break;
			case MidiDataType::Parameter:
				{
					uint32_t idx;
					if(!_parameters.getIndexByName(idx, b.name))
					{
						LOG("Failed to find named parameter " << b.name << " while parsing midi packet, midi byte " << i);
						return false;
					}
					_parameterValues.insert(std::make_pair(idx, s));
				}
				break;
			default:
				assert(false && "unknown data type");
				return false;
			}
		}
		return true;
	}
}
