#include "hdi08TxParser.h"

#include <chrono>
#include <sstream>
#include <thread>

#include "microcontroller.h"
#include "romfile.h"

#include "dsp56kBase/logging.h"

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
				LOG("DSP TX [Preset] No upgrade needed (F4F4F4)");
			}
			else if(_data == 0xf50000)
			{
				m_state = State::StatusReport;
				m_remainingStatusBytes = isABCFamily(m_mc.getROM().getModel()) ? 1 : 2;
				LOGTX("DSP TX [StatusReport] Begin (" << m_remainingStatusBytes << " words expected)");
			}
			else if(_data == 0xf400f4)
			{
				m_state = State::Preset;
				LOG("DSP TX [Preset] Begin receiving upgraded preset (F400F4)");

				m_presetData.clear();

				if(m_remainingPresetBytes == 0)
				{
					m_remainingPresetBytes = std::numeric_limits<uint32_t>::max();
					LOG("DSP TX [Preset] WARNING: No one requested a preset upgrade, guessing size from version byte");
				}
				else
				{
					LOG("DSP TX [Preset] Expecting " << m_remainingPresetBytes << " bytes");
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
							const auto p = static_cast<PatternType>(i);

							switch (p)
							{
							case PatternType::DspBoot:
								m_dspHasBooted = true;
								LOG("DSP TX [Boot] DSP boot completed (F40000 7F0000)");
								break;
							default:
								m_matchedPatterns.push_back(p);
								LOG("DSP TX [Pattern] Matched pattern " << i);
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
					if(m_nonPatternWords.size() == 1 && (_data & 0xffff) == 0)
					{
						LOGTX("DSP TX [MidiEcho] byte=" << HEXN(byte, 2) << std::dec);
					}
					else
					{
						std::stringstream s;
						for (const auto& w : m_nonPatternWords)
							s << HEX(w) << ' ';
						LOG("DSP TX [Unknown] Unhandled words: " << s.str());
					}

					m_nonPatternWords.clear();
				}
			}
			break;
		case State::Sysex:
			{
				if(_data & 0xffff)
				{
					LOG("DSP TX [SysEx] Abort: invalid midi byte " << HEX(_data) << " after " << m_sysexData.size() << " bytes, re-processing as Default");
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
					m_state = State::Default;

					// Detect Access Music complexity SysEx: F0 00 20 33 01 <data> <state> <complexity> <counter> <table_value> F7
					if(m_sysexData.size() == 11 && m_sysexData[1] == 0x00 && m_sysexData[2] == 0x20 && m_sysexData[3] == 0x33 && m_sysexData[4] == 0x01)
					{
						const auto complexity = m_sysexData[7];
						const auto state = m_sysexData[6];
						const auto counter = m_sysexData[8];
						LOG("DSP TX [Complexity] " << (static_cast<float>(complexity) / 64.0f) << " (index=" << static_cast<int>(complexity) << " state=" << HEXN(state,2) << std::dec << " counter=" << static_cast<int>(counter) << ")");
					}
					else
					{
						std::stringstream s;
						for (const auto b : m_sysexData)
							s << HEXN(b, 2) << ' ';
						LOG("DSP TX [SysEx] " << m_sysexData.size() << " bytes: " << s.str());
					}

					synthLib::SMidiEvent ev(synthLib::MidiEventSource::Device);
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
					LOG("DSP TX [Preset] Version=" << static_cast<int>(version) << ", size=" << m_remainingPresetBytes << " bytes");
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
					LOG("DSP TX [Preset] Upgrade complete, received " << m_presetData.size() << " bytes");
					m_state = State::Default;
				}
			}
			break;
		case State::StatusReport:
			m_dspStatus.push_back(_data);
			if(--m_remainingStatusBytes == 0)
			{
				if(m_dspStatus.size() >= 2)
				{
					LOGTX("DSP TX [StatusReport] overload=" << (m_dspStatus[m_dspStatus.size()-2] >> 16) << " data=" << HEX(m_dspStatus.back()));
				}
				else if(m_dspStatus.size() == 1)
				{
					LOGTX("DSP TX [StatusReport] data=" << HEX(m_dspStatus.back()));
				}
				m_dspStatus.clear();
				m_state = State::Default;
			}
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
