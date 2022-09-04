#pragma once

#include <string>

#include "mqdsp.h"
#include "mqmc.h"
#include "mqtypes.h"
#include "rom.h"

#include "dsp56kEmu/dspthread.h"

namespace mqLib
{
	class Hardware
	{
	public:
		explicit Hardware(std::string _romFilename);
		~Hardware();

		bool process();

		MqMc& getUC() { return m_uc; }
		MqDsp& getDSP() { return m_dsp; }
		uint64_t getUcCycles() const { return m_uc.getCycles(); }

		auto& getAudioInputs() { return m_audioInputs; }
		auto& getAudioOutputs() { return m_audioOutputs; }

		void sendMidi(uint8_t _byte);
		void sendMidi(const std::vector<uint8_t>& _data);
		void receiveMidi(std::vector<uint8_t>& _data);

		const dsp56k::DSPThread& getDspThread() { return m_dspThread; }

		void processAudio(uint32_t _frames);

		void ensureBufferSize(uint32_t _frames);

	private:
		void hdiProcessUCtoDSPNMIIrq();
		void hdiSendIrqToDSP(uint8_t _irq);
		void ucYield();
		void ucYieldLoop(const std::function<bool()>& _continue);
		bool hdiTransferDSPtoUC() const;
		void hdiTransferUCtoDSP(dsp56k::TWord _word);
		void waitDspRxEmpty();
		void onUCRxEmpty(bool needMoreData);
		void processUcCycle();
		void haltDSP();
		void resumeDSP();
		void setGlobalDefaultParameters();

		const std::string m_romFileName;

		const ROM m_rom;

		uint32_t m_hdiHF01 = 0;	// uc => DSP
		uint32_t m_hdiHF23 = 0;	// DSP => uc
		bool m_requestNMI = false;
		bool m_haveSentTXtoDSP = false;

		// timing
		uint32_t m_esaiFrameIndex = 0;
		uint32_t m_lastEsaiFrameIndex = 0;
		int32_t m_remainingUcCycles = 0;
		double m_remainingUcCyclesD = 0;

		MqMc m_uc;
		MqDsp m_dsp;
		dsp56k::DSPThread m_dspThread;

		mc68k::Hdi08& m_hdiUC;
		dsp56k::HDI08& m_hdiDSP;

		TAudioInputs m_audioInputs;
		TAudioOutputs m_audioOutputs;

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
