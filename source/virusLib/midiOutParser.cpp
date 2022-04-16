#include "midiOutParser.h"

#include <chrono>
#include <sstream>
#include <thread>

#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace virusLib
{
	bool MidiOutParser::append(const dsp56k::TWord word)
	{
//		LOG("HDI08 TX: " << HEX(word));

		// Only support for single byte responses atm
		if ((word & 0xff00ffff) != 0) 
		{
//			LOG("Unexpected MIDI data: 0x" << HEX(word));
			return false;
		}

		uint8_t buf = (word & 0x00ff0000) >> 16;

		// Check for sequence start 0xf0
		if (!m_data.empty()) 
		{
			if (buf == 0 || buf == 0xf5)
				return false;
			if (buf != 0xf0) {
//				LOG("Unexpected MIDI bytes: 0x" << HEXN(buf, 2));
				return false;
			}
		}

		m_data.push_back(buf);

		// End of midi command, show it
		if (buf == 0xf7) 
		{
			std::ostringstream stringStream;
			for (size_t i = 0; i < m_data.size(); i++) 
			{
				//printf("tmp: 0x%x\n", midi[i]);
				stringStream << HEXN(m_data[i], 2);
			}
			LOG("SYSEX RESPONSE: 0x" << stringStream.str());

			synthLib::SMidiEvent ev;
			std::swap(ev.sysex, m_data);
			m_midiData.push_back(ev);
			return true;
		}

		//LOG("BUF=0x"<< HEX(buf));

		return false;
	}
}
