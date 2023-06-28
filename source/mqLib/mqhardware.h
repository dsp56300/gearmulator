#pragma once

#include <string>

#include "mqbuildconfig.h"
#include "mqdsp.h"
#include "mqmc.h"
#include "mqtypes.h"
#include "mqmidi.h"
#include "rom.h"

#include "dsp56kEmu/dspthread.h"

#include "../synthLib/midiTypes.h"

namespace mqLib
{
	class Hardware
	{
		static constexpr uint32_t g_dspCount = g_useVoiceExpansion ? 3 : 1;

	public:
		explicit Hardware(std::string _romFilename);
		~Hardware();

		bool process();

		MqMc& getUC() { return m_uc; }
		MqDsp& getDSP(uint32_t _index = 0) { return m_dsps[_index]; }
		uint64_t getUcCycles() const { return m_uc.getCycles(); }

		auto& getAudioInputs() { return m_audioInputs; }
		auto& getAudioOutputs() { return m_audioOutputs; }

		void sendMidi(const synthLib::SMidiEvent& _ev);
		void receiveMidi(std::vector<uint8_t>& _data);

		dsp56k::DSPThread& getDspThread(uint32_t _index = 0) { return m_dsps[_index].thread(); }

		void processAudio(uint32_t _frames, uint32_t _latency = 0);

		void ensureBufferSize(uint32_t _frames);

		void ucThreadTerminated()
		{
			resumeDSP();
		}

		void setBootMode(BootMode _mode);

		bool isValid() const { return m_rom.isValid(); }

		bool isBootCompleted() const { return m_bootCompleted; }
		void resetMidiCounter();

		void ucYieldLoop(const std::function<bool()>& _continue);
		void initVoiceExpansion();

	private:
		void setupEsaiListener();
		void hdiProcessUCtoDSPNMIIrq();
		void processUcCycle();
		void haltDSP();
		void resumeDSP();
		void setGlobalDefaultParameters();
		void syncUcToDSP();
		void processMidiInput();

		const std::string m_romFileName;

		const ROM m_rom;

		bool m_requestNMI = false;

		// timing
		uint32_t m_esaiFrameIndex = 0;
		uint32_t m_lastEsaiFrameIndex = 0;
		int64_t m_remainingUcCycles = 0;
		double m_remainingUcCyclesD = 0;

		MqMc m_uc;
		std::array<MqDsp,g_dspCount> m_dsps;

		Midi m_midi;
		dsp56k::RingBuffer<synthLib::SMidiEvent, 1024, true> m_midiIn;
		uint32_t m_midiOffsetCounter = 0;

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
		bool m_processAudio = false;
		bool m_bootCompleted = false;
	};
}
