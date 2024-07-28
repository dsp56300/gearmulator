#pragma once

#include "n2xdsp.h"
#include "n2xmc.h"
#include "n2xrom.h"

namespace n2x
{
	class Hardware
	{
	public:
		using AudioOutputs = std::array<std::vector<dsp56k::TWord>, 4>;
		Hardware();

		bool isValid() const;

		void processUC();

		Microcontroller& getUC() {return m_uc; }

		const auto& getAudioOutputs() const { return m_audioOutputs; }

		void processAudio(uint32_t _frames, uint32_t _latency);

		auto& getDSPA() { return m_dspA; }
		auto& getDSPB() { return m_dspB; }

		void haltDSPs();
		void resumeDSPs();

		bool getButtonState(ButtonType _type) const;
		void setButtonState(ButtonType _type, bool _pressed);

	private:
		void ensureBufferSize(uint32_t _frames);
		void onEsaiCallbackA();
		void onEsaiCallbackB();
		void syncUCtoDSP();

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
	};
}
