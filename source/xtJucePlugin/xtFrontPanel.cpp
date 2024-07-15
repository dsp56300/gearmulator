#include "xtFrontPanel.h"

#include "xtController.h"
#include "xtEditor.h"
#include "xtLcd.h"

#include "xtLib/xtMidiTypes.h"

namespace xtJucePlugin
{
	constexpr const char* g_ledNames[] =
	{
		"midiLed"
	};

	FrontPanel::FrontPanel(const Editor& _editor, Controller& _controller) : m_controller(_controller)
	{
		m_leds.fill(nullptr);

		for(size_t i=0; i<std::size(g_ledNames); ++i)
			m_leds[i] = _editor.findComponentT<juce::Button>(g_ledNames[i], false);

		auto *lcdArea = _editor.findComponentT<juce::Component>("lcdArea", false);

		if (lcdArea)
			m_lcd.reset(new XtLcd(*lcdArea, m_controller));

		auto* shadow = _editor.findComponent("lcdshadow", false);

		if(shadow)
			shadow->setInterceptsMouseClicks(false, false);

		_controller.sendSysEx(Controller::EmuRequestLcd);
		_controller.sendSysEx(Controller::EmuRequestLeds);

		_controller.setFrontPanel(this);
	}

	FrontPanel::~FrontPanel()
	{
		m_controller.setFrontPanel(nullptr);
		m_lcd.reset();
	}

	void FrontPanel::processSysex(const std::vector<uint8_t>& _msg) const
	{
		if(_msg.size() < 5)
			return;

		const auto cmd = static_cast<xt::SysexCommand>(_msg[4]);

		switch (cmd)
		{
		case xt::SysexCommand::EmuLCD:
			processLCDUpdate(_msg);
			break;
		case xt::SysexCommand::EmuLEDs:
			processLedUpdate(_msg);
			break;
		case xt::SysexCommand::EmuRotaries:
		case xt::SysexCommand::EmuButtons:
		default:
			break;
		}
	}

	void FrontPanel::processLCDUpdate(const std::vector<uint8_t>& _msg) const
	{
		const auto* data = &_msg[5];

		std::array<uint8_t, 80> d{};

		for(size_t i=0; i<d.size(); ++i)
			d[i] = data[i];

		m_lcd->setText(d);
	}

	void FrontPanel::processLedUpdate(const std::vector<uint8_t>& _msg) const
	{
		const uint32_t leds = 
			(static_cast<uint32_t>(_msg[5]) << 24) |
			(static_cast<uint32_t>(_msg[6]) << 16) |
			(static_cast<uint32_t>(_msg[7]) << 8) |
			static_cast<uint32_t>(_msg[8]);

		for(size_t i=0; i<static_cast<uint32_t>(xt::LedType::Count); ++i)
		{
			if(m_leds[i])
				m_leds[i]->setToggleState((leds & (1<<i)) != 0, juce::dontSendNotification);
		}
	}
}