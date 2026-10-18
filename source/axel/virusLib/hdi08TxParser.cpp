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
		const auto isTI = m_mc.getROM().isTIFamily();

		// a TI OS puts these in front of everything that waits in its transmit ring, so they may arrive inside a packet
		if(isTI && m_state != State::Preset && (_data == 0xf4f4f4 || _data == 0xf400f4))
			return beginPreset(_data);

		switch (m_state)
		{
		case State::Default:
			if(isTI)
				return appendUsbMidi(_data);

			if(_data == 0xf4f4f4 || _data == 0xf400f4)
			{
				return beginPreset(_data);
			}
			else if(_data == 0xf50000)
			{
				m_state = State::StatusReport;
				m_remainingStatusBytes = 1;
				LOGTX("DSP TX [StatusReport] Begin (" << m_remainingStatusBytes << " words expected)");
			}
			else if((_data & 0xff0000) == 0xf00000)
			{
				LOGTX("Begin reading sysex");
				m_state = State::Sysex;
				m_sysexData.push_back(byte);
			}
			else if(byte < 0x80 ? m_midiStatus != 0 : byte < 0xf0 && !(_data & 0xffff))
			{
				// MIDI out, one byte per word, with running status. Reports of the OS in between leave it. Only notes
				// leave the device, the arpeggiator's with Midi Arp Send: unlike the TI, which keeps them on a cable of
				// their own, the A, B and C send the parameter changes they get as controllers on the same port
				if(byte >= 0x80)
				{
					m_midiStatus = byte;
					m_midiCount = 0;
					break;
				}

				m_midiBytes[m_midiCount++] = byte;

				const auto kind = m_midiStatus & 0xf0;
				if(m_midiCount < (kind == 0xc0 || kind == 0xd0 ? 1u : 2u))
					break;

				m_midiCount = 0;
				if(kind != synthLib::M_NOTEON && kind != synthLib::M_NOTEOFF)
					break;
				m_midiData.emplace_back(synthLib::MidiEventSource::Device, m_midiStatus, m_midiBytes[0], m_midiBytes[1]);
				return true;
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

				m_sysexData.push_back(byte);

				if(byte == 0xf7)
				{
					m_state = State::Default;
					return finishSysex();
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
					m_state = m_stateAfterPreset;
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

	bool Hdi08TxParser::beginPreset(const dsp56k::TWord _data)
	{
		if(_data == 0xf4f4f4)
		{
			m_remainingPresetBytes = 0;
			LOG("DSP TX [Preset] No upgrade needed (F4F4F4)");
			return false;
		}

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

		m_stateAfterPreset = m_state;
		m_state = State::Preset;
		return false;
	}

	// A TI OS sends USB MIDI 1.0 event packets, one byte per word: a header (cable << 4 | code index number) and three
	// data bytes. The low bytes of a word carry nothing, the velocity of a note-on has leftovers in them
	bool Hdi08TxParser::appendUsbMidi(const dsp56k::TWord _data)
	{
		const auto byte = static_cast<uint8_t>(_data >> 16);

		if(m_usbPacketSize == 0)
		{
			// the status report has no header
			if(_data == 0xf50000)
			{
				m_state = State::StatusReport;
				m_remainingStatusBytes = 2;
				LOGTX("DSP TX [StatusReport] Begin (" << m_remainingStatusBytes << " words expected)");
				return false;
			}

			// code index numbers 0 and 1 are reserved
			if((_data & 0xffff) || (byte & 0xf) < 2)
			{
				LOG("DSP TX [Unknown] Unhandled word, no USB MIDI packet header: " << HEX(_data));
				return false;
			}
		}

		m_usbPacket[m_usbPacketSize++] = byte;

		if(m_usbPacketSize < m_usbPacket.size())
			return false;

		m_usbPacketSize = 0;
		return processUsbMidiPacket();
	}

	bool Hdi08TxParser::processUsbMidiPacket()
	{
		// valid data bytes per code index number
		constexpr uint8_t sizes[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};

		const auto header = m_usbPacket[0];
		const auto cable = header >> 4;
		const auto cin = header & 0xf;
		const auto* data = &m_usbPacket[1];
		const auto size = sizes[cin];

		// F4 7F 00 F7, sent once after boot. Cable 15 is never used otherwise
		if(header == 0xf4)
		{
			m_dspHasBooted = true;
			LOG("DSP TX [Boot] DSP boot completed (F40000 7F0000)");
			return false;
		}

		// 4 starts or continues a SysEx, 5 to 7 end it with that many bytes minus 4. 5 carries a single byte system
		// common message too, F7 tells them apart
		if(cin == 0x4 || cin == 0x6 || cin == 0x7 || (cin == 0x5 && data[0] == 0xf7))
		{
			if(data[0] == 0xf0)
			{
				if(!m_sysexData.empty())
					LOG("DSP TX [SysEx] Abort: new SysEx begins after " << m_sysexData.size() << " bytes");
				m_sysexData.clear();
			}
			else if(m_sysexData.empty())
			{
				LOG("DSP TX [SysEx] Dropping a continuation without a start, cable " << cable);
				return false;
			}

			m_sysexData.insert(m_sysexData.end(), data, data + size);

			if(cin == 0x4)
				return false;
			return finishSysex();
		}

		// panel data are the parameter changes echoed for an editor, as controllers if MIDI control is enabled. The
		// editor knows them already, it would take one that comes back as an edit and the host stops its automation
		if(cable != USB_CABLE_MIDI_DATA)
		{
			LOGTX("DSP TX [Panel] " << HEXN(data[0], 2) << ' ' << HEXN(data[1], 2) << ' ' << HEXN(data[2], 2) << " on cable " << cable);
			return false;
		}

		m_midiData.emplace_back(synthLib::MidiEventSource::Device, data[0], size > 1 ? data[1] : 0, size > 2 ? data[2] : 0);
		return true;
	}

	bool Hdi08TxParser::finishSysex()
	{
		// Detect Access Music complexity SysEx: F0 00 20 33 01 <data> <state> <complexity> <counter> <table_value> F7.
		// On a TI this is the size of every parameter change, F0 00 20 33 01 <dev> <page> <part> <param> <value> F7
		if(!m_mc.getROM().isTIFamily() && m_sysexData.size() == 11 && m_sysexData[1] == 0x00 && m_sysexData[2] == 0x20 && m_sysexData[3] == 0x33 && m_sysexData[4] == 0x01)
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
