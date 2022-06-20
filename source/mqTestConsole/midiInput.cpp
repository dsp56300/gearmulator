#include "midiInput.h"

#include <array>

#include "../portmidi/pm_common/portmidi.h"

#include <chrono>

#include "dsp56kEmu/logging.h"

#ifndef Pm_MessageData3
#define Pm_MessageData3(msg) (((msg) >> 24) & 0xFF)
#endif

PmTimestamp returnTimeProc(void*)
{
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	return static_cast<PmTimestamp>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

MidiInput::MidiInput(const std::string& _deviceName) : MidiDevice(_deviceName)
{
	Device::openDevice();
}

MidiInput::~MidiInput()
{
	Pm_Close(m_stream);
	m_stream = nullptr;
	m_deviceId = -1;
}

int MidiInput::getDefaultDeviceId() const
{
	return Pm_GetDefaultInputDeviceID();
}

bool MidiInput::openDevice(int _devId)
{
	const auto err = Pm_OpenInput(&m_stream, _devId, nullptr, 8, returnTimeProc, this);

	if(err != pmNoError)
		LOG("Failed to open MIDI input device " << deviceNameFromId(_devId));

	return err == pmNoError;
}

bool MidiInput::process(std::vector<synthLib::SMidiEvent>& _events)
{
	if(Pm_Poll(m_stream) == pmNoData)
		return false;

	while(true)
	{
		PmEvent e;
		const auto count = Pm_Read(m_stream, &e, 1);

		if(!count)
			return false;

		if(count != 1)
			continue;

		process(_events, e.message);
	}
}

void MidiInput::process(std::vector<synthLib::SMidiEvent>& _events, uint32_t _message)
{
	const uint8_t bytes[] = {
		static_cast<uint8_t>(Pm_MessageStatus(_message)),
		static_cast<uint8_t>(Pm_MessageData1(_message)),
		static_cast<uint8_t>(Pm_MessageData2(_message)),
		static_cast<uint8_t>(Pm_MessageData3(_message))
	};

	for (const auto byte : bytes)
	{
		if(byte == synthLib::M_STARTOFSYSEX)
			m_readSysex = true;

		if(m_readSysex)
		{
			if(byte >= 0x80)
			{
				if(byte == synthLib::M_ENDOFSYSEX)
					m_sysexBuffer.push_back(byte);
				//else: aborted sysex

				m_readSysex = false;
				synthLib::SMidiEvent ev;
				std::swap(m_sysexBuffer, ev.sysex);
				m_sysexBuffer.clear();
				_events.emplace_back(ev);
				return;
			}
		}
	}

	if(!m_readSysex)
		_events.emplace_back(bytes[0], bytes[1], bytes[2]);
}
