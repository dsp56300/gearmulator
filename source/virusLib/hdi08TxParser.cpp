#include "hdi08TxParser.h"

#include <chrono>
#include <sstream>
#include <thread>

#include "microcontroller.h"
#include "romfile.h"

#include "dsp56kEmu/logging.h"

#define LOGTX(S)

namespace virusLib
{
	const std::vector<dsp56k::TWord> g_knownPatterns[] =
	{
		{0xf40000, 0x7f0000}		// sent after DSP has booted
	};

	bool Hdi08TxParser::append(const dsp56k::TWord _data)
	{
//		LOGTX("HDI08 TX: " << HEX(_data));

		const auto byte = static_cast<uint8_t>(_data >> 16);

		switch (m_state)
		{
		case State::Default:
			if(_data == 0xf4f4f4)
			{
				m_remainingPresetBytes = 0;
				m_state = State::Default;
				LOGTX("Finished receiving preset, no upgrade needed");
			}
			else if(_data == 0xf50000)
			{
				m_state = State::StatusReport;
				m_remainingStatusBytes = isABCFamily(m_mc.getROM().getModel()) ? 1 : 2;
			}
			else if(_data == 0xf400f4)
			{
				m_state = State::Preset;
				LOGTX("Begin receiving upgraded preset");

				m_presetData.clear();

				if(m_remainingPresetBytes == 0)
				{
					m_remainingPresetBytes = std::numeric_limits<uint32_t>::max();
					LOGTX("No one requested a preset upgrade, assuming preset size based on first word (version number)");
				}
			}
			else if((_data & 0xff0000) == 0xf00000)
			{
				LOGTX("Begin reading sysex");
				m_state = State::Sysex;
				m_sysexData.push_back(byte);
				m_sysexReceiveIndex = 1;
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
//							LOGTX("Matched pattern " << i);
							const auto p = static_cast<PatternType>(i);

							switch (p)
							{
							case PatternType::DspBoot:
								m_dspHasBooted = true;
								LOGTX("DSP boot completed");
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
					LOGTX("Unknown DSP words: " << s.str());
*/
					m_nonPatternWords.clear();
				}
			}
			break;
		case State::Sysex:
			{
				if(_data & 0xffff)
				{
					LOGTX("Abort reading sysex, received invalid midi byte " << HEX(_data));
					m_state = State::Default;
					m_midiData.clear();
					return append(_data);
				}

				// TI seems to send 3 valid bytes and then a fourth invalid one, no idea what this is good for, drop it
				++m_sysexReceiveIndex;
				if(m_mc.getROM().isTIFamily() && (m_sysexReceiveIndex & 3) == 0)
					return true;

				m_sysexData.push_back(byte);

				if(byte == 0xf7)
				{
					LOGTX("End reading sysex");

					m_state = State::Default;

					std::stringstream s;
					for (const auto b : m_sysexData)
						s << HEXN(b, 2);

					LOGTX("Received sysex: " << s.str());

					synthLib::SMidiEvent ev(synthLib::MidiEventSource::Plugin);
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
					LOGTX("Preset size for version code " << static_cast<int>(version) << " is " << m_remainingPresetBytes);
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
					LOGTX("Succesfully received preset");
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

	void Hdi08TxParser::getPresetData(std::vector<uint8_t>& _data)
	{
		std::swap(m_presetData, _data);
		m_presetData.clear();
	}
}
