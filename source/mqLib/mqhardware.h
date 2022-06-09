#pragma once

#include <string>

#include "mqdsp.h"
#include "mqmc.h"
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

		void process(uint32_t _frames);

	private:
		void transferHostFlags();
		void dspExecCallback();
		void injectUCtoDSPInterrupts();
		void ucYield();
		bool hdiTransferDSPtoUC();
		void hdiTransferUCtoDSP();
		void onUCRxEmpty(bool needMoreData);
		void processUcCycle();
		void processAudio();

		const std::string m_romFileName;

		const ROM m_rom;

		MqMc m_uc;
		MqDsp m_dsp;

		mc68k::Hdi08& m_hdiUC;
		dsp56k::HDI08& m_hdiDSP;
		Buttons& m_buttons;
		dsp56k::DSPThread m_dspThread;

		uint32_t m_hdiHF01 = 0;	// uc => DSP
		uint32_t m_hdiHF23 = 0;	// DSP => uc
		uint32_t m_dspCycles = 0;
		uint32_t m_dspInstructionCounter = 0;
		bool m_requestNMI = false;
		bool m_haveSentTXtoDSP = false;

		std::deque<uint32_t> txData;

		std::array<std::vector<dsp56k::TWord>, 2> m_audioInputs;
		std::array<std::vector<dsp56k::TWord>, 6> m_audioOutputs;

		// timing
		uint32_t m_esaiFrameIndex = 0;
		uint32_t m_lastEsaiFrameIndex = 0;
		int32_t m_remainingMcCycles = 0;
		uint32_t m_requestedSampleFrames = 0;
	};
}
