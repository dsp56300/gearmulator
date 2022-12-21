#include "hdi08TxParser.h"

#include <chrono>
#include <sstream>
#include <thread>

#include "microcontroller.h"
#include "romfile.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace virusLib
{
	const std::vector<dsp56k::TWord> g_knownPatterns[] =
	{
		{0xf40000, 0x7f0000}		// sent after DSP has booted
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
				m_remainingPresetBytes = 0;
				m_state = State::Default;
				LOG("Finished receiving preset, no upgrade needed");
			}
			else if(_data == 0xf50000)
			{
				m_state = State::StatusReport;
				m_remainingStatusBytes = m_mc.getROM().getModel() == ROMFile::Model::ABC ? 1 : 2;
			}
			else if(_data == 0xf400f4)
			{
				m_state = State::Preset;
				LOG("Begin receiving upgraded preset");

				if(m_remainingPresetBytes == 0)
				{
					m_remainingPresetBytes = std::numeric_limits<uint32_t>::max();
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

				m_nonPatternWords.emplace_back(_data);

				for (const auto& pattern : g_knownPatterns)
				{
					auto& pos = m_patternPositions[i];

					if(pattern[pos] == _data)
					{
						matched = true;

						++pos;
						if(pos == std::size(pattern))
						{
//							LOG("Matched pattern " << i);
							const auto p = static_cast<PatternType>(i);

							switch (p)
							{
							case PatternType::DspBoot:
								m_dspHasBooted = true;
								LOG("DSP boot completed");
								break;
							default:
								m_matchedPatterns.push_back(p);
								break;
							}

							pos = 0;
							m_nonPatternWords.clear();
						}
					}
					else
					{
						pos = 0;
					}

					++i;
				}

				if(!matched)
				{
/*					std::stringstream s;
					for (const auto& w : m_nonPatternWords)
						s << HEX(w) << ' ';
					LOG("Unknown DSP words: " << s.str());
*/
					m_nonPatternWords.clear();
				}
			}
			break;
		case State::Sysex:
			{
				if(_data & 0xffff)
				{
					LOG("Abort reading sysex, received invalid midi byte " << HEX(_data));
					m_state = State::Default;
					m_midiData.clear();
					return append(_data);
				}

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
			break;
		case State::Preset:
			{
				if(m_remainingPresetBytes == std::numeric_limits<uint32_t>::max())
				{
					const auto version = byte;

					switch (version)
					{
					case 1:
					case 2:
						m_remainingPresetBytes = m_mc.getROM().getMultiPresetSize();
						break;
					default:
						m_remainingPresetBytes = m_mc.getROM().getSinglePresetSize();
						break;
					}
					LOG("Preset size for version code " << static_cast<int>(version) << " is " << m_remainingPresetBytes);
				}

				uint32_t shift = 16;
				uint32_t i=0;
				while(m_remainingPresetBytes > 0 && i < 3)
				{
					m_presetData.push_back((_data >> shift) & 0xff);
					shift -= 8;
					--m_remainingPresetBytes;
					++i;
				}

				if(m_remainingPresetBytes == 0)
				{
					LOG("Succesfully received preset");
					m_state = State::Default;
				}
			}
			break;
		case State::StatusReport:
			m_dspStatus.push_back(_data);
			if(--m_remainingStatusBytes == 0)
				m_state = State::Default;
			break;
		}

		return false;
	}

	void Hdi08TxParser::waitForPreset(uint32_t _byteCount)
	{
		m_remainingPresetBytes = _byteCount;
	}
}
