#pragma once

#include "portmidi.h"
#include "syx.h"

#define INPUT_BUFFER_SIZE 0
#define OUTPUT_BUFFER_SIZE 32
#define LATENCY 0

namespace virusLib
{
	class Midi
	{
	public:
		explicit Midi(Syx& _syx);
		int connect();
		void listen();
	private:
		PmStream* m_midiIn;
		PmStream* m_midiOut;
		const Syx& m_syx;
	};
}

