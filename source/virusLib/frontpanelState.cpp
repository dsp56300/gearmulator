#include "frontpanelState.h"

#include "microcontrollerTypes.h"

#include "synthLib/midiTypes.h"

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/peripherals.h"

namespace virusLib
{
	static constexpr std::initializer_list<uint8_t> g_midiDumpHeader = {0xf0, 0x00, 0x20, 0x33, 0x01, OMNI_DEVICE_ID, DUMP_EMU_SYNTHSTATE};

	void FrontpanelState::updateLfoPhaseFromTimer(dsp56k::DSP& _dsp, const uint32_t _lfo, const uint32_t _timer, const float _minimumValue/* = 0.0f*/, float _maximumValue/* = 1.0f*/)
	{
		updatePhaseFromTimer(m_lfoPhases[_lfo], _dsp, _timer, _minimumValue, _maximumValue);
	}

	void FrontpanelState::updatePhaseFromTimer(float& _target, dsp56k::DSP& _dsp, const uint32_t _timer, float _minimumValue, float _maximumValue)
	{
		const auto* peripherals = _dsp.getPeriph(0);

		if(const auto* p362 = dynamic_cast<const dsp56k::Peripherals56362*>(peripherals))
		{
			const auto& t = p362->getTimers();
			updatePhaseFromTimer(_target, t, _timer, _minimumValue, _maximumValue);
		}
		else if(const auto* p303 = dynamic_cast<const dsp56k::Peripherals56303*>(peripherals))
		{
			const auto& t = p303->getTimers();
			updatePhaseFromTimer(_target, t, _timer, _minimumValue, _maximumValue);
		}
	}

	void FrontpanelState::updatePhaseFromTimer(float& _target, const dsp56k::Timers& _timers, uint32_t _timer, float _minimumValue, float _maximumValue)
	{
		const auto compare = _timers.readTCPR(static_cast<int>(_timer));
		const auto load = _timers.readTLR(static_cast<int>(_timer));

		const auto range = 0xffffff - load;

		const auto normalized = static_cast<float>(compare - load) / static_cast<float>(range);

		// the minimum PWM value is not always zero, we need to remap
		const auto floatRange = _maximumValue - _minimumValue;
		const auto floatRangeInv = 1.0f / floatRange;

		_target = (normalized - _minimumValue) * floatRangeInv;
	}

	void writeFloat(std::vector<uint8_t>& _sysex, const float _v)
	{
		const auto* ptr = reinterpret_cast<const uint8_t*>(&_v);
		for(size_t i=0; i<sizeof(_v); ++i)
			_sysex.push_back(ptr[i]);
	}

	size_t readFloat(float& _dst, const uint8_t* _s)
	{
		auto* ptr = reinterpret_cast<uint8_t*>(&_dst);
		for(size_t i=0; i<sizeof(_dst); ++i)
			ptr[i] = _s[i];
		return sizeof(_dst);
	}

	void FrontpanelState::toMidiEvent(synthLib::SMidiEvent& _e) const
	{
		_e.sysex.assign(g_midiDumpHeader.begin(), g_midiDumpHeader.end());

		uint32_t midiEvents = 0;

		for(size_t i=0; i<m_midiEventReceived.size(); ++i)
		{
			if(m_midiEventReceived[i])
				midiEvents |= (1<<i);
		}

		_e.sysex.push_back((midiEvents >> 14) & 0x7f);
		_e.sysex.push_back((midiEvents >> 7) & 0x7f);
		_e.sysex.push_back((midiEvents) & 0x7f);

		for (const auto lfoPhase : m_lfoPhases)
			writeFloat(_e.sysex, lfoPhase);

		writeFloat(_e.sysex, m_logo);
		writeFloat(_e.sysex, m_bpm);

		_e.sysex.push_back(0xf7);
	}

	bool FrontpanelState::fromMidiEvent(const synthLib::SMidiEvent& _e)
	{
		return fromMidiEvent(_e.sysex);
	}

	bool FrontpanelState::fromMidiEvent(const std::vector<uint8_t>& _sysex)
	{
		if(_sysex.size() < g_midiDumpHeader.size())
			return false;

		const auto* s = &_sysex[g_midiDumpHeader.size()];

		uint32_t midiEvents = 0;
		midiEvents |= static_cast<uint32_t>(*s) << 14; ++s;
		midiEvents |= static_cast<uint32_t>(*s) << 7; ++s;
		midiEvents |= static_cast<uint32_t>(*s); ++s;

		for(size_t i=0; i<m_midiEventReceived.size(); ++i)
			m_midiEventReceived[i] = (midiEvents & (1<<i)) ? true : false;

		for (auto& lfoPhase : m_lfoPhases)
			s += readFloat(lfoPhase, s);

		s += readFloat(m_logo, s);
		readFloat(m_bpm, s);

		return true;
	}
}
