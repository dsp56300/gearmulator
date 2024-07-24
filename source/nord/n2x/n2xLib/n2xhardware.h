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
		void ucYieldLoop(const std::function<bool()>& _continue);

		const auto& getAudioOutputs() const { return m_audioOutputs; }

		void processAudio(uint32_t _frames, uint32_t _latency);

		void resumeDSPsForFunc(const std::function<void()>& _callback)
		{
			const auto halted = m_haltDSP;
			if(halted)
				resumeDSP();
			_callback();
			if(halted)
				haltDSP();
		}

	private:
		void ensureBufferSize(uint32_t _frames);
		void onEsaiCallback();
		void syncUCtoDSP();
		void haltDSP();
		void resumeDSP();

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
		bool m_haltDSP = false;
		std::condition_variable m_haltDSPcv;
		std::mutex m_haltDSPmutex;
	};
}
