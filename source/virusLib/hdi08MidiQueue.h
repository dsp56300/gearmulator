#pragma once

#include "dsp56kEmu/ringbuffer.h"

#include "../synthLib/midiTypes.h"

namespace dsp56k
{
	class Esai;
}

namespace synthLib
{
	struct SMidiEvent;
}

namespace virusLib
{
	class Hdi08Queue;
	class DspSingle;

	class Hdi08MidiQueue
	{
	public:
		explicit Hdi08MidiQueue(DspSingle& _dsp, Hdi08Queue& _output, bool _useEsaiBasedTiming);
		explicit Hdi08MidiQueue(Hdi08MidiQueue&& _s) noexcept : m_output(_s.m_output), m_esai(_s.m_esai), m_useEsaiBasedTiming(_s.m_useEsaiBasedTiming)
		{
			assert(_s.m_pendingMidiEvents.empty());
			_s.m_useEsaiBasedTiming = false;
		}
		~Hdi08MidiQueue();

		void sendPendingMidiEvents(uint32_t _maxOffset);

		void add(const synthLib::SMidiEvent& ev);

		void onAudioWritten();

	private:
		void sendMidiToDSP(uint8_t _a, uint8_t _b, uint8_t _c) const;

		Hdi08Queue& m_output;
		dsp56k::Esai& m_esai;
		bool m_useEsaiBasedTiming;

		dsp56k::RingBuffer<synthLib::SMidiEvent, 1024, false> m_pendingMidiEvents;

		uint32_t m_numSamplesWritten = 0;
	};
}
