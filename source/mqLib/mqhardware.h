#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mqbuildconfig.h"
#include "mqdsp.h"
#include "mqmc.h"
#include "mqtypes.h"
#include "rom.h"

#include "dsp56kEmu/dspthread.h"

#include "hardwareLib/sciMidi.h"

#include "wLib/wHardware.h"

namespace mqLib
{
	class Hardware : public wLib::Hardware
	{
	public:
		explicit Hardware(const ROM& _rom, bool _voiceExpansion = false);
		~Hardware();

		void process();

		MqMc& getUC() { return m_uc; }
		MqDsp& getDSP(uint32_t _index = 0) { return *m_dsps[_index]; }
		uint64_t getUcCycles() const { return m_uc.getCycles(); }

		auto& getAudioInputs() { return m_audioInputs; }
		auto& getAudioOutputs() { return m_audioOutputs; }

		dsp56k::DSPThread& getDspThread(uint32_t _index = 0) { return m_dsps[_index]->thread(); }

		uint32_t getDspCount() const { return static_cast<uint32_t>(m_dsps.size()); }
		bool useVoiceExpansion() const { return m_useVoiceExpansion; }

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

		void initVoiceExpansion();

		hwLib::SciMidi& getMidi() override
		{
			return m_midi;
		}

		mc68k::Mc68k& getUc() override
		{
			return m_uc;
		}

	private:
		void setupEsaiListener();
		void hdiProcessUCtoDSPNMIIrq();
		void processUcCycle();
		void setGlobalDefaultParameters();

		const ROM m_rom;
		const bool m_useVoiceExpansion;

		bool m_requestNMI = false;
		bool m_dspResetPending = false;
		bool m_voiceExpansionReady = false;

		MqMc m_uc;
		TAudioInputs m_audioInputs;
		TAudioOutputs m_audioOutputs;
		std::vector<std::unique_ptr<MqDsp>> m_dsps;

		hwLib::SciMidi m_midi;
	};
}
