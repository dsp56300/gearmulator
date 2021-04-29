#include "midi.h"
#include "midiTypes.h"

using namespace dsp56k;

namespace virusLib
{
Midi::Midi(Syx& _syx) : m_syx(_syx)
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

void Midi::listen()
{
	PmError result;
	PmEvent buffer; /* just one message at a time */
	std::vector<uint8_t> sysex_buffer;
	sysex_buffer.resize(550);
	size_t idx = 0;
	bool sysex_in_progress = false;

	LOG("Listening for MIDI events..");
	while (true)
	{
		result = Pm_Poll(m_midiIn);
		if (!result) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		if (Pm_Read(m_midiIn, &buffer, 1) == pmBufferOverflow)
			continue;

		if (sysex_in_progress || Pm_MessageStatus(buffer.message) == M_STARTOFSYSEX)
		{
			// Handle sysex messages
			sysex_in_progress = true;
			int shift = 0, buf = 0;
			for (; shift <= 24; shift += 8) {
				buf	= (buffer.message >> shift) & 0xff;
				if (idx == 0 && buf == 0)
					continue;

				sysex_buffer[idx++] = buf; // TODO: check max buffer size

				if (buf == M_ENDOFSYSEX) {
					auto response = m_syx.sendSysex(sysex_buffer);
					if (response.size() > 0)
					{
						LOG("Sending sysex response")
						Pm_WriteSysEx(m_midiOut, 0, reinterpret_cast<unsigned char *>(response.data()));
					}

					// Clear data
					sysex_buffer.clear();
					sysex_buffer.resize(550);
					sysex_in_progress = false;
					idx = 0;
				}
			}
		} else {
			// Regular MIDI
			int status, data1, data2;
			status = Pm_MessageStatus(buffer.message);
			data1 = Pm_MessageData1(buffer.message);
			data2 = Pm_MessageData2(buffer.message);
			LOGFMT("Regular MIDI received {%02x,%02x,%02x}", status, data1, data2);
			m_syx.sendMIDI(status, data1, data2);
		}
	}
}

}