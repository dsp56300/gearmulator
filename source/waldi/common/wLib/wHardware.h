#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "dsp56kEmu/types.h"

#include "synthLib/midiInputQueue.h"
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

		// Request that any active ucYieldLoop exits immediately.
		// Used during shutdown so the UC thread can check m_destroy.
		void requestUcTermination();

		// The UC thread ended: nothing gives the DSP frames any more, it runs free from here on
		void ucThreadTerminated();

		void sendMidi(const synthLib::SMidiEvent& _ev);
		void receiveMidi(std::vector<uint8_t>& _data);

		uint32_t getEsaiFrameIndex() const { return m_esaiFrameIndex; }

	protected:
		void onEsaiCallback(dsp56k::Audio& _audio);
		void syncUcToDSP();
		void grantFrame(uint32_t _frame);
		void setUcWaitsForDsp(bool _wait);
		void wakeDspIfWaiting();
		bool dspHeldByGrant() const;	// under m_haltDSPmutex
		bool dspMayRun() const;			// under m_haltDSPmutex
		void processMidiInput();

		// Derived-class processAudio must bracket its body with these.
		// Begin sets the flag and wakes any UC thread sleeping in ucYieldLoop.
		// End clears the flag; no wake needed because waiters only sleep while
		// the flag is false.
		void beginProcessAudio();
		void endProcessAudio();

		// timing
		const double m_samplerateInv;
		// The DSP thread advances it, the UC thread reads it in the ucYieldLoop spin condition outside any mutex;
		// plain would let the read be hoisted out of that loop, which spins there for the rest of the session
		std::atomic<uint32_t> m_esaiFrameIndex{0};
		uint32_t m_lastEsaiFrameIndex = 0;
		int64_t m_remainingUcCycles = 0;
		double m_remainingUcCyclesD = 0;

		synthLib::MidiInputQueue m_midiIn;
		uint32_t m_midiOffsetCounter = 0;

		std::vector<dsp56k::TWord> m_dummyInput;
		std::vector<dsp56k::TWord> m_dummyOutput;

		std::mutex m_esaiFrameAddedMutex;
		dsp56k::ConditionVariable m_esaiFrameAddedCv;

		std::mutex m_requestedFramesAvailableMutex;
		dsp56k::ConditionVariable m_requestedFramesAvailableCv;
		size_t m_requestedFrames = 0;

		bool m_haltDSP = false;
		dsp56k::ConditionVariable m_haltDSPcv;
		std::mutex m_haltDSPmutex;

		// The DSP waits in onEsaiCallback while it is more than g_syncHaltDspEsaiThreshold frames ahead of the frame the UC
		// has been given cycles for. Without that, a UC thread that the host does not schedule for a while fell
		// arbitrarily far behind the audio: MIDI clock reached it bunched up and its tempo ticks reached the DSPs late, so
		// clocked LFOs ran at the wrong rate
		std::atomic<uint32_t> m_ucGrantedFrame{0};
		std::atomic<bool> m_ucGranting{false};		// from the first grant until the UC stops
		std::atomic<bool> m_ucWaitsForDsp{false};	// the UC waits in ucYieldLoop, no frame limit meanwhile
		std::atomic<bool> m_dspWaitsForUc{false};	// the DSP waits in onEsaiCallback, see wakeDspIfWaiting

		// Paired with m_processAudioCv/Mutex.  The audio callback flips this
		// to true for the duration of a buffer; ucYieldLoop uses it to decide
		// between spinning (audio active → low latency) and sleeping on the
		// CV (audio idle → save CPU).
		std::atomic<bool> m_processAudio{false};
		std::mutex m_processAudioMutex;
		dsp56k::ConditionVariable m_processAudioCv;

		bool m_bootCompleted = false;
		std::atomic<bool> m_terminateUcThread{false};
	};
}
