#pragma once

#include <string>

#include "mqdsp.h"
#include "mqmc.h"
#include "mqtypes.h"
#include "rom.h"

#include "dsp56kEmu/dspthread.h"

#include "../synthLib/audioTypes.h"

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
		uint64_t getDspCycles() const { return m_dspCycles; }
		const auto& getAudioOutputs() { return m_audioOutputs; }

		void sendMidi(uint8_t _byte);
		void receiveMidi(std::vector<uint8_t>& _data);

		const dsp56k::DSPThread& getDspThread() { return m_dspThread; }

		void processAudio(uint32_t _frames);

	private:
		void injectUCtoDSPInterrupts();
		void ucYield();
		bool hdiTransferDSPtoUC();
		void hdiTransferUCtoDSP();
		void onUCRxEmpty(bool needMoreData);
		void processUcCycle();

		const std::string m_romFileName;

		const ROM m_rom;

		uint32_t m_hdiHF01 = 0;	// uc => DSP
		uint32_t m_hdiHF23 = 0;	// DSP => uc
		uint64_t m_dspCycles = 0;
		uint32_t m_dspInstructionCounter = 0;
		bool m_requestNMI = false;
		bool m_haveSentTXtoDSP = false;

		// timing
		uint32_t m_esaiFrameIndex = 0;
		uint32_t m_lastEsaiFrameIndex = 0;
		int32_t m_remainingUcCycles = 0;

		MqMc m_uc;
		MqDsp m_dsp;
		dsp56k::DSPThread m_dspThread;

		mc68k::Hdi08& m_hdiUC;
		dsp56k::HDI08& m_hdiDSP;

		std::deque<uint32_t> m_txData;

		TAudioInputs m_audioInputs;
		TAudioOutputs m_audioOutputs;

		std::mutex m_ucWakeupMutex;
		std::condition_variable m_ucWakeupCv;
	};
}
