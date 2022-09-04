#include "demoplayback.h"

#include <cassert>
#include <vector>

#include "microcontroller.h"

#include "../synthLib/midiToSysex.h"
#include "../synthLib/midiTypes.h"
#include "../synthLib/os.h"

#include "dsp56kEmu/logging.h"

#include <cstring> // memcpy

#include "demopacketvalidator.h"

namespace virusLib
{
	constexpr auto g_timeScale_C = 57;	// C OS 6.6
	constexpr auto g_timeScale_A = 54;	// A OS 2.8

	bool DemoPlayback::loadMidi(const std::string& _filename)
	{
		if(synthLib::hasExtension(_filename, ".bin"))
		{
			std::vector<uint8_t> data;
			auto hFile = fopen(_filename.c_str(), "rb");
			if(!hFile)
			{
				LOG("Failed to open demo file " << _filename);
				return false;
			}
			fseek(hFile, 0, SEEK_END);
			data.resize(ftell(hFile));
			fseek(hFile, 0, SEEK_SET);
			fread(&data[0], 1, data.size(), hFile);
			fclose(hFile);
			return loadBinData(data);
		}

		std::vector<uint8_t> sysex;

		synthLib::MidiToSysex::readFile(sysex, _filename.c_str());

		if(sysex.empty())
		{
			LOG("Failed to load demo midi file " << _filename << ", no sysex data found in file");
			return false;
		}

		std::vector<std::vector<uint8_t>> packets;
		synthLib::MidiToSysex::splitMultipleSysex(packets, sysex);

		DemoPacketValidator validator;

		for (const auto& packet : packets)
			validator.add(packet);

		if(!validator.isValid())
		{
			LOG("Packet validation failed, packets missing or invalid");
			return false;
		}

		const auto& data = validator.getData();

		return loadBinData(data);
	}

	bool DemoPlayback::loadBinData(const std::vector<uint8_t>& _data)
	{
		// the start is either a raw serial packet or midi sysex packet so find that data
		for(size_t i=0; i<_data.size() - 6; ++i)
		{
			if(_data[i] != 0xf0)
				continue;

			if(	(_data[i+1] == 0x75 && _data[i+2] == 0x55) || 
				(_data[i+1] == 0x00 && _data[i+2] == 0x20 && _data[i+3] == 0x33 && _data[i+4] == 0x01))
			{
				auto data = _data;
				data.erase(data.begin(), data.begin() + i);
				return parseData(data);
			}
		}
		return false;
	}

	bool DemoPlayback::parseData(const std::vector<uint8_t>& _data)
	{
		for(size_t i=0; i<_data.size();)
		{
			Event e;

			switch(_data[i])
			{
			case 0xf0:
				for(size_t j=i+1; j<_data.size(); ++j)
				{
					if(_data[j] == 0xf7)
					{
						e = parseSysex(&_data[i], static_cast<uint32_t>(j - i + 1));
						i = j+1;
						break;
					}
				}
				break;
			default:
				e = parseMidi(&_data[i]);
				i += e.data.size();

				if(e.data[0] == synthLib::M_STOP)
					return true;
			}

			if(e.data.empty())
				break;

			// the byte that follows the data gives the delay for the next packet
			e.delay = _data[i++];

			m_events.push_back(e);
		}

		return false;
	}

	DemoPlayback::Event DemoPlayback::parseMidi(const uint8_t* _data)
	{
		Event e;

		e.type = EventType::Midi;

		const auto status = _data[0] < 0xf0 ? (_data[0] & 0xf0) : _data[0];

		e.data.push_back(_data[0]);

		switch (status)
		{
		case synthLib::M_ACTIVESENSING:	// most probably used to define a pause that is > 0xff
		case synthLib::M_STOP:			// end of demo
			break;
		case synthLib::M_AFTERTOUCH:
			e.data.push_back(_data[1]);
			break;
		case synthLib::M_QUARTERFRAME:
			// two bytes for a quarter frame event is not valid normally. Assuming this is some playback speed definition?
			// We use it for that. As the base is unknown we hardcode it for now
			{
				const auto a = _data[1];
				const auto b = _data[2];

				switch(a << 8 | b)
				{
				case 0xeaed:
					m_timeScale = g_timeScale_A;
					break;
				default:
					m_timeScale = g_timeScale_C;
					break;
				}
				e.data.push_back(a);
				e.data.push_back(b);
			}
			break;
		default:
			assert(status <= synthLib::M_PITCHBEND);
			e.data.push_back(_data[1]);
			e.data.push_back(_data[2]);
		}
		return e;
	}

	DemoPlayback::Event DemoPlayback::parseSysex(const uint8_t* _data, const uint32_t _count)
	{
		Event e;

		if(_data[1] == 0x75 && _data[2] == 0x55)
		{
			// This is not really sysex but arbitrary data that is sent directly to the DSP via HDI08
			// Only seen for single and multi patches for now
			e.data.resize(_count);
			memcpy(&e.data.front(), _data, _count);
			e.type = EventType::RawSerial;
		}
		else
		{
			// regular midi sysex data, sent to the uC
			e.data.resize(_count);
			memcpy(&e.data.front(), _data, _count);
			e.type = EventType::MidiSysex;
		}
		return e;
	}

	void DemoPlayback::process(const uint32_t _samples)
	{
		if(m_currentEvent == 0 && m_remainingDelay == 0)
		{
			// switch to multi mode when playback starts
			Microcontroller::TPreset data;
			m_mc.requestMulti(BankNumber::A, 0, data);
			m_mc.writeMulti(BankNumber::EditBuffer, 0, data);
			--m_remainingDelay;
			return;
		}

		if(m_currentEvent >= m_events.size())
			return;

		m_remainingDelay -= _samples;
		while(m_remainingDelay <= 0 && m_currentEvent < m_events.size())
		{
			const auto& e = m_events[m_currentEvent];
			if(!processEvent(e))
				return;
			++m_currentEvent;
			m_remainingDelay = e.delay * m_timeScale;
		}
	}

	bool DemoPlayback::processEvent(const Event& _event) const
	{
		switch (_event.type)
		{
		case EventType::MidiSysex:
			{
				std::vector<synthLib::SMidiEvent> responses;
				m_mc.sendSysex(_event.data, false, responses, synthLib::MidiEventSourcePlugin);
			}
			break;
		case EventType::Midi:
			{
				synthLib::SMidiEvent ev;
				ev.a = _event.data[0];
				ev.b = _event.data.size() > 1 ? _event.data[1] : 0;
				ev.c = _event.data.size() > 2 ? _event.data[2] : 0;

				if(ev.a != synthLib::M_ACTIVESENSING && ev.a != synthLib::M_QUARTERFRAME && ev.a != synthLib::M_STOP)
				{
					m_mc.sendMIDI(ev);
					m_mc.sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
				}
			}
			break;
		case EventType::RawSerial:
			{
				if(m_mc.needsToWaitForHostBits(0,1))
					return false;

				std::vector<dsp56k::TWord> dspWords;

				for(size_t i=0; i<_event.data.size(); i += 3)
				{
					dsp56k::TWord d = static_cast<dsp56k::TWord>(_event.data[i]) << 16;
					if(i+1 < _event.data.size())
						d |= static_cast<dsp56k::TWord>(_event.data[i+1]) << 8;
					if(i+2 < _event.data.size())
						d |= static_cast<dsp56k::TWord>(_event.data[i+2]);
					dspWords.push_back(d);
				}

				m_mc.writeHostBitsWithWait(0,1);
				m_mc.m_hdi08.writeRX(dspWords);
			}
			break;
		}
		return true;
	}
}
