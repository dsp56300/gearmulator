#pragma once

#include <string>

#include "mqbuildconfig.h"
#include "mqdsp.h"
#include "mqmc.h"
#include "mqtypes.h"
#include "rom.h"

#include "dsp56kEmu/dspthread.h"

#include "synthLib/midiTypes.h"

#include "wLib/wMidi.h"
#include "wLib/wHardware.h"

namespace mqLib
{
	class Hardware : public wLib::Hardware
	{
		static constexpr uint32_t g_dspCount = g_useVoiceExpansion ? 3 : 1;

	public:
		explicit Hardware(const std::string& _romFilename);
		~Hardware();

		void process();

		MqMc& getUC() { return m_uc; }
		MqDsp& getDSP(uint32_t _index = 0) { return m_dsps[_index]; }
		uint64_t getUcCycles() const { return m_uc.getCycles(); }

		auto& getAudioInputs() { return m_audioInputs; }
		auto& getAudioOutputs() { return m_audioOutputs; }

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

		void initVoiceExpansion();

		wLib::Midi& getMidi() override
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

		bool m_requestNMI = false;

		MqMc m_uc;
		TAudioInputs m_audioInputs;
		TAudioOutputs m_audioOutputs;
		std::array<MqDsp,g_dspCount> m_dsps;

		wLib::Midi m_midi;
	};
}
