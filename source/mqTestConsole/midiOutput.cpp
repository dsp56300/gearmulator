#include "midiOutput.h"

#include "midiInput.h"
#include "../portmidi/pm_common/portmidi.h"
#include "../synthLib/midiTypes.h"
#include "dsp56kEmu/logging.h"

extern PmTimestamp returnTimeProc(void*);

MidiOutput::MidiOutput(const std::string& _deviceName) : MidiDevice(_deviceName, true)
{
	Device::openDevice();
}

int MidiOutput::getDefaultDeviceId() const
{
	return Pm_GetDefaultOutputDeviceID();
}

bool MidiOutput::openDevice(int devId)
{
	const auto err = Pm_OpenOutput(&m_stream, devId, nullptr, 1024, returnTimeProc, nullptr, 0);
	if(err != pmNoError)
		LOG("Failed to open Midi output " << devId);
	return err == pmNoError;
}

MidiOutput::~MidiOutput()
{
	Pm_Close(m_stream);
	m_stream = nullptr;
	m_deviceId = -1;
}

void MidiOutput::write(const std::vector<uint8_t>& _data)
{
	m_parser.write(_data);
	std::vector<synthLib::SMidiEvent> events;
	m_parser.getEvents(events);
	write(events);
}

void MidiOutput::write(const std::vector<synthLib::SMidiEvent>& _events) const
{
	for (const auto& e : _events)
	{
		if(!e.sysex.empty())
		{
			LOG("MIDI Out Write Sysex of length " << e.sysex.size());
			const auto err = Pm_WriteSysEx(m_stream, 0, const_cast<unsigned char*>(&e.sysex.front()));
			if(err != pmNoError)
			{
				LOG("Failed to send sysex, err " << err << " => " << Pm_GetErrorText(err));
			}
		}
		else
		{
			PmEvent ev;
			ev.message = Pm_Message(e.a, e.b, e.c);
			LOG("MIDI Out Write " << HEXN(e.a, 2) << " " << HEXN(e.b, 2) << ' ' << HEXN(e.c, 2));
			Pm_Write(m_stream, &ev, 1);
		}
	}
}
