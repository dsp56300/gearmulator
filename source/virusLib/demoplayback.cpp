#include "demoplayback.h"

#include <cassert>
#include <vector>

#include "microcontroller.h"

#include "../synthLib/midiToSysex.h"
#include "../synthLib/midiTypes.h"
#include "../synthLib/os.h"

#include "dsp56kEmu/logging.h"

#include <cstring> // memcpy

#include "midiFileToRomData.h"

namespace virusLib
{
	constexpr auto g_timeScale_C = 57;	// C OS 6.6
	constexpr auto g_timeScale_A = 54;	// A OS 2.8

	bool DemoPlayback::loadFile(const std::string& _filename)
	{
		if(synthLib::hasExtension(_filename, ".bin"))
		{
			std::vector<uint8_t> data;
			auto* hFile = fopen(_filename.c_str(), "rb");
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

		MidiFileToRomData romReader;
		if(!romReader.load(_filename) || romReader.getData().empty())
		{
			LOG("Failed to load demo midi file " << _filename << ", no valid data found in file");
			return false;
		}

		return loadBinData(romReader.getData());
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

	DemoPlayback::Event DemoPlayback::parseSysex(const uint8_t* _data, const uint32_t _count) const
	{
		Event e;

		if(_data[1] == 0x75 && _data[2] == 0x55)
		{
			// This is not really sysex but arbitrary data that is sent directly to the DSP via HDI08
			// Only seen for single and multi patches for now
			e.data.resize(_count);
			memcpy(&e.data.front(), _data, _count);

#if 0	// demo presets extraction
			if(_count - 6 >= ROMFile::getSinglePresetSize())
			{
				int foo=0;
				ROMFile::TPreset data;
				memcpy(&data[0], &e.data[6], ROMFile::getSinglePresetSize());

				const auto isMulti = _data[3] == 0x11;
				const uint8_t program = _data[4];

				const auto name = isMulti ? ROMFile::getMultiName(data) : ROMFile::getSingleName(data);

				std::vector<synthLib::SMidiEvent> responses;

				// use the uc to generate our sysex header
				if(isMulti)
				{
					m_mc.sendSysex({0xf0, 0x00, 0x20, 0x33, 0x01, OMNI_DEVICE_ID, 0x31, 0x01, 0x00, 0xf7}, responses, synthLib::MidiEventSourceEditor);
				}
				else
				{
					m_mc.sendSysex({0xf0, 0x00, 0x20, 0x33, 0x01, OMNI_DEVICE_ID, 0x30, 0x01, program, 0xf7}, responses, synthLib::MidiEventSourceEditor);
				}

				auto& s = responses.front().sysex;
				memcpy(&s[9], &data[0], data.size());

				// checksum needs to be updated
				s.pop_back();
				Microcontroller::calcChecksum(s, 5);
				s.push_back(0xf7);

				std::stringstream ss;
				ss << "demo_preset_" << (isMulti ? "multi" : "single") << '_' << std::setfill('0') << std::setw(2) << std::to_string(program) << '_' << name << ".syx";

				auto filename = ss.str();
				for(auto& f : filename)
				{
					switch (f)
					{
					case '?':
					case '@':
					case ';':
					case ':':
					case '/':
					case '\\':
						f = '_';
						break;
					}
				}
				FILE* hFile = fopen(filename.c_str(), "wb");
				fwrite(&s.front(), 1, s.size(), hFile);
				fclose(hFile);
			}
#endif
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
		if(m_stop)
			return;

		if(m_currentEvent == 0 && m_remainingDelay == 0)
		{
			// switch to multi mode when playback starts
			Microcontroller::TPreset data;
			m_mc.requestMulti(BankNumber::A, 0, data);
			m_mc.writeMulti(BankNumber::EditBuffer, 0, data);
			--m_remainingDelay;
			return;
		}

		if(m_currentEvent >= getEventCount())
			return;

		m_remainingDelay -= static_cast<int32_t>(_samples);

		while(m_remainingDelay <= 0)
		{
			if(!processEvent(m_currentEvent))
				return;

			m_remainingDelay = static_cast<int32_t>(static_cast<float>(getEventDelay(m_currentEvent)) * m_timeScale);

			++m_currentEvent;

			if(m_currentEvent >= getEventCount())
			{
				stop();
				break;
			}
		}
	}

	void DemoPlayback::writeRawData(const std::vector<uint8_t>& _data) const
	{
		std::vector<dsp56k::TWord> dspWords;

		for(size_t i=0; i<_data.size(); i += 3)
		{
			dsp56k::TWord d = static_cast<dsp56k::TWord>(_data[i]) << 16;
			if(i+1 < _data.size())
				d |= static_cast<dsp56k::TWord>(_data[i+1]) << 8;
			if(i+2 < _data.size())
				d |= static_cast<dsp56k::TWord>(_data[i+2]);
			dspWords.push_back(d);
		}

		m_mc.writeHostBitsWithWait(0,1);
		m_mc.m_hdi08.writeRX(dspWords);
	}

	void DemoPlayback::stop()
	{
		m_stop = true;
		LOG("Demo Playback end reached");
		std::cout << "Demo song has ended." << std::endl;
	}

	bool DemoPlayback::processEvent(const Event& _event) const
	{
		switch (_event.type)
		{
		case EventType::MidiSysex:
			{
				std::vector<synthLib::SMidiEvent> responses;
				m_mc.sendSysex(_event.data, responses, synthLib::MidiEventSourcePlugin);
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
			writeRawData(_event.data);
			break;
		}
		return true;
	}
}
