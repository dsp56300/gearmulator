#include "leds.h"

#include "dsp56kEmu/logging.h"

namespace mqLib
{
	bool Leds::exec(const mc68k::Port& _portF, const mc68k::Port& _portGP, const mc68k::Port& _portE)
	{
		bool changed = false;

		if(_portE.getDirection() & (1<<6))
		{
			const uint32_t powerLedState = (_portE.read() >> 6) & 1;

			if(powerLedState != m_powerLedState)
			{
				changed = true;
				m_powerLedState = powerLedState;
			}
		}

		if(!(_portF.getDirection() & (1<<7)))
			return ret(changed);

		if(_portGP.getDirection() != 0xff)
			return ret(changed);

		const auto prevF7 = m_stateF7;
		const auto stateF7 = _portF.read() & (1<<7);
		m_stateF7 = stateF7;

		if(!stateF7 || prevF7)
			return ret(changed);

		const auto gp = _portGP.read();
		const auto group = (gp >> 5) - 1;
		const auto leds = gp & 0x1f;
		
		if(group >= 5)
			return ret(changed);

		uint32_t ledIndex = group;

		for(auto i=0; i<5; ++i)
		{
			changed |= setLed(ledIndex, (leds >> i) & 1);

			ledIndex += 5;
		}

		return ret(changed);
	}

	bool Leds::setLed(uint32_t _index, uint32_t _value)
	{
		auto& led = m_ledState[_index];
		const auto prev = led;
		led = _value;

		if(led == prev)
			return false;

//		LOG("LED " << _index << " changed to " << _value);
		return true;
	}

	bool Leds::ret(const bool _changed) const
	{
		if(!_changed)
			return false;
		if(m_changeCallback)
			m_changeCallback();
		return true;
	}
}
