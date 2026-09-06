#pragma once

#include "n2xtypes.h"
#include "mc68k/peripheralBase.h"

namespace n2x
{
	class Hardware;
	class FrontPanel;

	template<uint32_t Base>
	class FrontPanelCS : public mc68k::PeripheralBase<Base, g_frontPanelSize>
	{
	public:
		explicit FrontPanelCS(FrontPanel& _fp);

	protected:
		FrontPanel& m_panel;
	};

	class FrontPanelCS4 : public FrontPanelCS<g_frontPanelAddressCS4>
	{
	public:
		explicit FrontPanelCS4(FrontPanel& _fp);

		uint8_t read8(mc68k::PeriphAddress _addr) override;
		uint8_t getKnobPosition(KnobType _knob) const;
		void setKnobPosition(KnobType _knob, uint8_t _value);

		std::array<uint8_t, static_cast<uint32_t>(KnobType::Last) - static_cast<uint32_t>(KnobType::First) + 1> m_knobPositions;
	};

	class FrontPanelCS6 : public FrontPanelCS<g_frontPanelAddressCS6>
	{
	public:
		explicit FrontPanelCS6(FrontPanel& _fp);

		void write8(mc68k::PeriphAddress _addr, uint8_t _val) override;
		uint8_t read8(mc68k::PeriphAddress _addr) override;

		auto getKnobType() const { return m_selectedKnob; }

		void setButtonState(ButtonType _button, bool _pressed);
		bool getButtonState(ButtonType _button) const;

	private:
		void printLCD() const;
		void onLCDChanged();

		uint8_t m_ledLatch8 = 0;
		uint8_t m_ledLatch10 = 0;
		uint8_t m_ledLatch12 = 0;
		std::array<uint8_t,3> m_lcds{0,0,0};
		std::array<uint8_t,3> m_lcdsPrev{0,0,0};
		KnobType m_selectedKnob = KnobType::PitchBend;

		std::array<uint8_t, 4> m_buttonStates;
	};

	class FrontPanel
	{
	public:
		FrontPanel(Hardware& _hardware);

		auto& cs4() { return m_cs4; }
		auto& cs6() { return m_cs6; }

		bool isInRange(const mc68k::PeriphAddress _pa) const
		{
			return m_cs4.isInRange(_pa) || m_cs6.isInRange(_pa);
		}

		bool getButtonState(ButtonType _type) const
		{
			return m_cs6.getButtonState(_type);
		}

		void setButtonState(ButtonType _type, bool _pressed)
		{
			m_cs6.setButtonState(_type, _pressed);
		}

		Hardware& getHardware() const { return m_hardware; }

		uint8_t getKnobPosition(KnobType _knob) const
		{
			return m_cs4.getKnobPosition(_knob);
		}
		void setKnobPosition(KnobType _knob, uint8_t _value)
		{
			m_cs4.setKnobPosition(_knob, _value);
		}

	private:
		Hardware& m_hardware;
		FrontPanelCS4 m_cs4;
		FrontPanelCS6 m_cs6;
	};
}
