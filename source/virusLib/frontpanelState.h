#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace synthLib
{
	struct SMidiEvent;
}

namespace dsp56k
{
	class Timers;
	class DSP;
}

namespace virusLib
{
	struct FrontpanelState
	{
		FrontpanelState()
		{
			clear();
		}

		void clear()
		{
			m_midiEventReceived.fill(false);
			m_lfoPhases.fill(0.0f);
			m_bpm = 0;
			m_logo = 0;
		}

		void updateLfoPhaseFromTimer(dsp56k::DSP& _dsp, uint32_t _lfo, uint32_t _timer, float _minimumValue = 0.0f, float _maximumValue = 1.0f);
		static void updatePhaseFromTimer(float& _target, dsp56k::DSP& _dsp, uint32_t _timer, float _minimumValue = 0.0f, float _maximumValue = 1.0f);
		static void updatePhaseFromTimer(float& _target, const dsp56k::Timers& _timers, uint32_t _timer, float _minimumValue = 0.0f, float _maximumValue = 1.0f);

		void toMidiEvent(synthLib::SMidiEvent& _e) const;
		bool fromMidiEvent(const synthLib::SMidiEvent& _e);
		bool fromMidiEvent(const std::vector<uint8_t>& _sysex);

		std::array<bool, 16> m_midiEventReceived;
		std::array<float, 3> m_lfoPhases;

		float m_logo = 0.0f;
		float m_bpm = 0.0f;
	};
}
