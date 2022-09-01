#pragma once

#include <array>
#include <functional>

namespace mc68k
{
	class Port;
}
namespace mqLib
{
	class Leds
	{
	public:
		using ChangeCallback = std::function<void()>;

		enum class Led
		{
			// group			1				2			3				4			5
			/* led index 0 */	Osc1,			Filters2,	Inst1,			Global,		Play,			
			/* led index 1 */	Osc2,			AmpFx,		Inst2,			Multi,		Peek,		
			/* led index 2 */	Osc3,			Env1,		Inst3,			Edit,		Multimode,	
			/* led index 3 */	MixerRouting,	Env2,		Inst4,			Sound,		Shift,		
			/* led index 4 */	Filters1,		Env3,		ModMatrix,		LFOs,		Env4,
			Power,
			Count
		};

		Leds() = default;
		bool exec(const mc68k::Port& _portF, const mc68k::Port& _portGP, const mc68k::Port& _portE);

		auto getLedState(Led _led) const { return m_ledState[static_cast<uint32_t>(_led)]; }
		
		void setChangeCallback(const ChangeCallback& _callback)
		{
			m_changeCallback = _callback;
		}
	private:
		bool setLed(uint32_t _index, uint32_t _value);
		bool ret(bool _changed) const;

		std::array<uint32_t, static_cast<uint32_t>(Led::Count)> m_ledState;
		uint32_t m_stateF7;
		ChangeCallback m_changeCallback;
	};
}
