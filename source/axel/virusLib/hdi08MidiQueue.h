#pragma once

#include <atomic>

#include "dsp56kBase/ringbuffer.h"

#include "dsp56kEmu/types.h"

#include "synthLib/midiTypes.h"

namespace dsp56k
{
	class Audio;
	class Memory;
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
		explicit Hdi08MidiQueue(DspSingle& _dsp, Hdi08Queue& _output, bool _useEsaiBasedTiming, bool _isTI, bool _needsMidiHeartbeat = false);
		explicit Hdi08MidiQueue(Hdi08MidiQueue&& _s) noexcept : m_output(_s.m_output), m_esai(_s.m_esai), m_memory(_s.m_memory), m_useEsaiBasedTiming(_s.m_useEsaiBasedTiming), m_isTI(_s.m_isTI), m_needsMidiHeartbeat(_s.m_needsMidiHeartbeat), m_dspTimeAddr(_s.m_dspTimeAddr.load())
		{
			assert(_s.m_pendingMidiEvents.empty());
			_s.m_useEsaiBasedTiming = false;
		}
		~Hdi08MidiQueue();

		void sendPendingMidiEvents(uint32_t _maxOffset);

		void add(const synthLib::SMidiEvent& ev);

		void onAudioWritten();

		// TI family, call once the OS runs: send every event with the sample at which the OS is to play it
		bool enableTimestamps();

	private:
		void sendMidiToDSP(uint8_t _a, uint8_t _b, uint8_t _c);
		void updateDspTime();

		Hdi08Queue& m_output;
		dsp56k::Audio& m_esai;
		dsp56k::Memory& m_memory;
		bool m_useEsaiBasedTiming;
		bool m_isTI;
		bool m_needsMidiHeartbeat;

		dsp56k::RingBuffer<synthLib::SMidiEvent, 1024, false> m_pendingMidiEvents;

		uint32_t m_numSamplesWritten = 0;
		uint32_t m_lastMidiSample = 0;

		std::atomic<dsp56k::TWord> m_dspTimeAddr{0};	// x: address of the OS sample counter, 0 = no timestamps
		uint32_t m_dspTime = 0;							// that counter in samples, 14 bits like a timestamp
		uint32_t m_dspTimeStep = 0;						// samples per OS block, 0 while unknown
		uint32_t m_dspTimeFrame = 0;					// frame at which the counter took its value, 0 = not seen yet
		uint32_t m_lastTimestampFrame = 0;				// frame of the last timestamp sent
	};
}
