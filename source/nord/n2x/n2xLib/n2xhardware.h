#pragma once

#include "n2xdsp.h"
#include "n2xmc.h"
#include "n2xrom.h"

#include "synthLib/audioTypes.h"
#include "synthLib/midiTypes.h"

namespace n2x
{
	class Hardware
	{
	public:
		using AudioOutputs = std::array<std::vector<dsp56k::TWord>, 4>;
		Hardware();
		~Hardware();

		bool isValid() const;

		void processUC();

		Microcontroller& getUC() {return m_uc; }

		const auto& getAudioOutputs() const { return m_audioOutputs; }

		void processAudio(uint32_t _frames, uint32_t _latency);

		auto& getDSPA() { return m_dspA; }
		auto& getDSPB() { return m_dspB; }

		auto& getMidi() { return m_uc.getMidi(); }

		void haltDSPs();
		void resumeDSPs();
		bool requestingHaltDSPs() const { return m_dspHalted; }

		bool getButtonState(ButtonType _type) const;
		void setButtonState(ButtonType _type, bool _pressed);

		uint8_t getKnobPosition(KnobType _knob) const;
		void setKnobPosition(KnobType _knob, uint8_t _value);

		void processAudio(const synthLib::TAudioOutputs& _outputs, uint32_t _frames, uint32_t _latency);
		bool sendMidi(const synthLib::SMidiEvent& _ev);
		void notifyBootFinished();

		const std::string& getRomFilename() const { return m_rom.getFilename(); }

	private:
		void ensureBufferSize(uint32_t _frames);
		void onEsaiCallbackA();
		void processMidiInput();
		void onEsaiCallbackB();
		void syncUCtoDSP();
		void ucThreadFunc();
		void advanceSamples(uint32_t _samples, uint32_t _latency);

		Rom m_rom;
		Microcontroller m_uc;
		DSP m_dspA;
		DSP m_dspB;

		std::vector<dsp56k::TWord> m_dummyInput;
		std::vector<dsp56k::TWord> m_dummyOutput;
		std::vector<dsp56k::TWord> m_dspAtoBBuffer;

		AudioOutputs m_audioOutputs;

		// timing
		const double m_samplerateInv;
		uint32_t m_esaiFrameIndex = 0;
		uint32_t m_lastEsaiFrameIndex = 0;
		int64_t m_remainingUcCycles = 0;
		double m_remainingUcCyclesD = 0;
		std::mutex m_esaiFrameAddedMutex;
		std::condition_variable m_esaiFrameAddedCv;
		std::mutex m_requestedFramesAvailableMutex;
		std::condition_variable m_requestedFramesAvailableCv;
		size_t m_requestedFrames = 0;
		bool m_dspHalted = false;
		dsp56k::SpscSemaphore m_semDspAtoB;

		dsp56k::RingBuffer<dsp56k::Audio::RxFrame, 4, true> m_dspAtoBbuf;

		std::unique_ptr<std::thread> m_ucThread;
		bool m_destroy = false;

		// Midi
		dsp56k::RingBuffer<synthLib::SMidiEvent, 16384, true> m_midiIn;
		uint32_t m_midiOffsetCounter = 0;

		// DSP slowdown
		uint32_t m_maxEsaiCallbacks = 0;
		uint32_t m_esaiLatency = 0;
		std::mutex m_haltDSPmutex;
		std::condition_variable m_haltDSPcv;

		bool m_bootFinished = false;
	};
}
