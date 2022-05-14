#include "midipacket.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace pluginLib
{
	bool MidiPacket::create(std::vector<uint8_t>& _dst, std::map<MidiDataType, uint8_t>& _data) const
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
}
