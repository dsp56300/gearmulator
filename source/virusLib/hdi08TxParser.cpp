#include "hdi08TxParser.h"

#include <chrono>
#include <sstream>
#include <thread>

#include "romfile.h"
#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace virusLib
{
	const std::vector<dsp56k::TWord> g_knownPatterns[] =
	{
		{0xf40000, 0x7f0000, 0x000000, 0xf70000},		// sent after DSP has booted
		{0xf50000, 0x010000},							// sent all the time, beat indicator? (on)
		{0xf50000, 0x000000},							// sent all the time, beat indicator? (off)
	};

	bool Hdi08TxParser::append(const dsp56k::TWord _data)
	{
//		LOG("HDI08 TX: " << HEX(_data));

		const auto byte = static_cast<uint8_t>(_data >> 16);

		switch (m_state)
		{
		case State::Default:
			if(_data == 0xf4f4f4)
			{
				m_waitForPreset = 0;
				m_state = State::Default;
				LOG("Finished receiving preset, no upgrade needed");
			}
			else if(_data == 0xf400f4)
			{
				m_state = State::Preset;
				LOG("Begin receiving upgraded preset");

				if(m_waitForPreset == 0)
				{
					m_waitForPreset = std::numeric_limits<uint32_t>::max();
					LOG("No one requested a preset upgrade, assuming preset size based on first word (version number)");
				}
			}
			else if((_data & 0xff0000) == 0xf00000)
			{
				LOG("Begin reading sysex");
				m_state = State::Sysex;
				m_sysexData.push_back(byte);
			}
			else
			{
				size_t i=0;
				bool matched = false;
				for (const auto& pattern : g_knownPatterns)
				{
					auto& pos = m_patternPositions[i++];

					if(pattern[pos] == _data)
					{
						matched = true;

						++pos;
						if(pos == std::size(pattern))
						{
//							LOG("Matched pattern " << i);
							m_matchedPatterns.push_back(static_cast<PatternType>(i));
							pos = 0;
						}
					}
					else
					{
						pos = 0;
					}
				}

				if(!matched)
					LOG("Unknown DSP word " << _data);
			}
			break;
		case State::Sysex:
			{
				if(_data & 0xffff)
				{
					LOG("Abort reading sysex, received invalid midi byte " << HEX(_data));
					m_state = State::Default;
					m_midiData.clear();
				}
				else
				{
					m_sysexData.push_back(byte);

					if(byte == 0xf7)
					{
						LOG("End reading sysex");

						m_state = State::Default;

						std::stringstream s;
						for (const auto b : m_sysexData)
							s << HEXN(b, 2);

						LOG("Received sysex: " << s.str());

						synthLib::SMidiEvent ev;
						std::swap(ev.sysex, m_sysexData);
						m_midiData.emplace_back(ev);

						return true;
					}
				}
			}
			break;
		case State::Preset:
			{
				if(m_waitForPreset == std::numeric_limits<uint32_t>::max())
				{
					const auto version = byte;

					switch (version)
					{
					case 1:
					case 2:
						m_waitForPreset = ROMFile::getMultiPresetSize();
						break;
					default:
						m_waitForPreset = ROMFile::getSinglePresetSize();
						break;
					}
					LOG("Preset size for version code " << static_cast<int>(version) << " is " << m_waitForPreset);
				}

				uint32_t shift = 16;
				uint32_t i=0;
				while(m_waitForPreset > 0 && i < 3)
				{
					m_presetData.push_back((_data >> shift) & 0xff);
					shift -= 8;
					--m_waitForPreset;
					++i;
				}

				if(m_waitForPreset == 0)
				{
					LOG("Succesfully received preset");
					m_state = State::Default;
				}
			}

			break;
		}

		return false;
	}

	void Hdi08TxParser::waitForPreset(uint32_t _byteCount)
	{
		m_waitForPreset = _byteCount;
	}
}
