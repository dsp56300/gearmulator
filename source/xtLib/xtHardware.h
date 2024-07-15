#pragma once

#include <string>

#include "xtDSP.h"
#include "xtRom.h"
#include "xtUc.h"

#include "dsp56kEmu/dspthread.h"

#include "wLib/wMidi.h"
#include "wLib/wHardware.h"

namespace xt
{
	class XtUc;

	class Hardware : public wLib::Hardware
	{
		static constexpr uint32_t g_dspCount = 1;

	public:
		explicit Hardware();
		virtual ~Hardware();

		void process();

		XtUc& getUC() { return m_uc; }
		DSP& getDSP(uint32_t _index = 0) { return m_dsps[_index]; }
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
		void processUcCycle();

		const Rom m_rom;

		XtUc m_uc;
		TAudioInputs m_audioInputs;
		TAudioOutputs m_audioOutputs;
		std::array<DSP,g_dspCount> m_dsps;
		wLib::Midi m_midi;
	};
}
