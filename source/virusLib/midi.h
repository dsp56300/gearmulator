#pragma once

#include "portmidi.h"
#include "syx.h"
#include "midiTypes.h"

#define INPUT_BUFFER_SIZE 0
#define OUTPUT_BUFFER_SIZE 32
#define LATENCY 0

namespace virusLib
{
	class Midi
	{
	public:
		typedef int MidiError;
		typedef enum MidiErrorCode
		{
			midiNoError = 0,
			midiNoData = 0, /**< A "no error" return that also indicates no data avail. */
			midiGotData = 1, /**< A "no error" return that also indicates data available */
		} MidiErrorCode;

		explicit Midi();
		int connect();
		MidiError read(SMidiEvent& _midiIn);
		void write(const SMidiEvent& _data);
	private:
		PmStream* m_midiIn;
		PmStream* m_midiOut;
		std::vector<uint8_t> sysex_buffer;
		size_t sysex_idx = 0;
		bool sysex_in_progress = false;
	};
}

