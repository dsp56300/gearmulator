#include "midi.h"

using namespace dsp56k;

namespace virusLib
{
Midi::Midi()
{
	Pm_Initialize();

	auto deviceCount = Pm_CountDevices();
	LOG("Available MIDI devices:")
	for (int i = 0 ; i < deviceCount; ++i)
	{
		const PmDeviceInfo* info = Pm_GetDeviceInfo(i);
		LOG(" => " << info->name << ", input=" << info->input << ", output=" << info->output)
	}
}

int Midi::connect()
{
	int id;
	const PmDeviceInfo *info;

	id = Pm_GetDefaultOutputDeviceID();
	info = Pm_GetDeviceInfo(id);
	if (info == NULL) {
		LOGFMT("Could not open default output device 0x%x: %d", id, info);
		return -1;
	}

	LOG("Opening output device " << info->name);
	Pm_OpenOutput(&m_midiOut, id, NULL, OUTPUT_BUFFER_SIZE, NULL, NULL, LATENCY);

	id = Pm_GetDefaultInputDeviceID();
	info = Pm_GetDeviceInfo(id);
	if (info == NULL) {
		LOGFMT("Could not open default input device 0x%x: %d", id, info);
		return -1;
	}

	LOG("Opening input device " << info->name);
	Pm_OpenInput(&m_midiIn, id, NULL, INPUT_BUFFER_SIZE, NULL, NULL);

	return 0;
}

Midi::MidiError Midi::read(SMidiEvent& _data)
{
	PmError result;
	PmEvent buffer; /* just one message at a time */

	while (true)
	{
		result = Pm_Poll(m_midiIn);
		if (!result) {
			return midiNoData;
		}

		if (Pm_Read(m_midiIn, &buffer, 1) == pmBufferOverflow)
			continue;

		if (Pm_MessageStatus(buffer.message) == M_STARTOFSYSEX)
		{
			sysex_buffer.clear();
			sysex_buffer.resize(550);
			sysex_idx = 0;
			sysex_in_progress = true;
		}

		if (sysex_in_progress)
		{
			// Handle sysex messages
			int shift = 0, buf = 0;
			for (; shift <= 24; shift += 8) {
				buf	= (buffer.message >> shift) & 0xff;
				if (sysex_idx == 0 && buf == 0)
					continue;

				sysex_buffer[sysex_idx++] = buf; // TODO: check max buffer size

				if (buf == M_ENDOFSYSEX) {
					_data.sysex = sysex_buffer;
					sysex_in_progress = false;
					return midiGotData;
				}
			}
		} else {
			// Regular MIDI
			_data.a = Pm_MessageStatus(buffer.message);
			_data.b = Pm_MessageData1(buffer.message);
			_data.c = Pm_MessageData2(buffer.message);
//			midiEvent.offset = ev->deltaFrames;
			return midiGotData;
		}
	}

	return midiNoData;
}

void Midi::write(const SMidiEvent& _data)
{
	if (!_data.sysex.empty())
	{
		LOG("Sending sysex response")
		Pm_WriteSysEx(m_midiOut, 0, const_cast<unsigned char *>(_data.sysex.data()));
	}
}

}