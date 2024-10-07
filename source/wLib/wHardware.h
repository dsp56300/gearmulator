#pragma once

#include <cstdint>
#include <functional>

#include "dsp56kEmu/ringbuffer.h"
#include "dsp56kEmu/types.h"

#include "synthLib/midiTypes.h"

namespace hwLib
{
	class SciMidi;
}

namespace mc68k
{
	class Mc68k;
}

namespace dsp56k
{
	class Audio;
}

namespace wLib
{
	class Hardware
	{
	public:
		Hardware(const double& _samplerate);
		virtual ~Hardware() = default;

		virtual hwLib::SciMidi& getMidi() = 0;
		virtual mc68k::Mc68k& getUc() = 0;

		void haltDSP();
		void resumeDSP();

		void ucYieldLoop(const std::function<bool()>& _continue);

		void sendMidi(const synthLib::SMidiEvent& _ev);
		void receiveMidi(std::vector<uint8_t>& _data);

	protected:
		void onEsaiCallback(dsp56k::Audio& _audio);
		void syncUcToDSP();
		void processMidiInput();

		// timing
		const double m_samplerateInv;
		uint32_t m_esaiFrameIndex = 0;
		uint32_t m_lastEsaiFrameIndex = 0;
		int64_t m_remainingUcCycles = 0;
		double m_remainingUcCyclesD = 0;

		dsp56k::RingBuffer<synthLib::SMidiEvent, 16384, true> m_midiIn;
		uint32_t m_midiOffsetCounter = 0;

		std::vector<dsp56k::TWord> m_dummyInput;
		std::vector<dsp56k::TWord> m_dummyOutput;

		std::mutex m_esaiFrameAddedMutex;
		std::condition_variable m_esaiFrameAddedCv;

		std::mutex m_requestedFramesAvailableMutex;
		std::condition_variable m_requestedFramesAvailableCv;
		size_t m_requestedFrames = 0;

		bool m_haltDSP = false;
		std::condition_variable m_haltDSPcv;
		std::mutex m_haltDSPmutex;
		bool m_processAudio = false;
		bool m_bootCompleted = false;
	};
}
