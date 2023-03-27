#include "mqFrontPanel.h"

#include "mqController.h"
#include "mqEditor.h"
#include "mqLcd.h"
#include "mqLcdText.h"

#include "../mqLib/device.h"
#include "../mqLib/mqmiditypes.h"

namespace mqLib
{
	enum class SysexCommand : uint8_t;
}

namespace mqJucePlugin
{
	constexpr float g_encoderSpeed = 2.0f;

	constexpr const char* g_ledNames[] =
	{
		"ledOsc1", "ledFilter2", "ledInst1", "ledGlobal", "ledPlay",
		"ledOsc2", "ledAmpFxArp", "ledInst2", "ledMulti", "ledPeek",
		"ledOsc3", "ledEnv1", "ledInst3", "ledEdit", "ledMultimode",
		"ledMixerRouting", "ledEnv2", "ledInst4", "ledSound", "ledShift",
		"ledFilter1", "ledEnv3", "ledModMatrix", "ledLFOs", "ledEnv4",
		"ledPower"
	};

	constexpr const char* g_buttonNames[] =
	{
		"buttonInst1", "buttonInst2", "buttonInst3", "buttonInst4",
		"buttonD", "buttonL", "buttonR", "buttonU", "buttonGlobal",
		"buttonMulti", "buttonEdit", "buttonSound", "buttonShift",
		"buttonMultimode", "buttonPeek", "buttonPlay", "buttonPower"
	};

	constexpr const char* g_encoderNames[] =
	{
		"encoderMatrix4",	"encoderMatrix3",	"encoderMatrix2",	"encoderMatrix1",
		"encoderLCDRight",	"encoderLCDLeft",
		"encoderAlphaDial"
	};

	FrontPanel::FrontPanel(const mqJucePlugin::Editor& _editor, Controller& _controller) : m_controller(_controller)
	{
		_controller.setFrontPanel(this);

		for(size_t i=0; i<std::size(g_ledNames); ++i)
			m_leds[i] = _editor.findComponentT<juce::Button>(g_ledNames[i], false);

		for(size_t i=0; i<std::size(g_buttonNames); ++i)
		{
			auto* b = _editor.findComponentT<juce::Button>(g_buttonNames[i], false);
			m_buttons[i] = b;
			if(!b)
				continue;

			const auto index = static_cast<uint32_t>(i);
			b->onStateChange = [this, index]
			{
				onButtonStateChanged(index);
			};
		}

		for(size_t i=0; i<std::size(g_encoderNames); ++i)
		{
			auto* e = _editor.findComponentT<juce::Slider>(g_encoderNames[i], false);
			if(!e)
				continue;

			m_encoders[i] = e;

			e->setRotaryParameters(0.0f, juce::MathConstants<float>::twoPi, false);
			const auto index = static_cast<uint32_t>(i);
			e->onValueChange = [this, index]
			{
				onEncoderValueChanged(index);
			};
		}

		auto *lcdArea = _editor.findComponentT<juce::Component>("lcdArea", false);

		if (lcdArea)
			m_lcd.reset(new MqLcd(*lcdArea));

		std::array<juce::Label*, 2> lcdLines{};

		lcdLines[0] = _editor.findComponentT<juce::Label>("lcdLineA", false);
		lcdLines[1] = _editor.findComponentT<juce::Label>("lcdLineB", lcdLines[0] != nullptr);

		if (lcdLines[0])
		{
			if (m_lcd)
			{
				lcdLines[0]->setVisible(false);
				lcdLines[1]->setVisible(false);
			}
			else
			{
				m_lcd.reset(new MqLcdText(*lcdLines[0], *lcdLines[1]));
			}
		}

		_controller.sendSysEx(Controller::EmuRequestLcd);
		_controller.sendSysEx(Controller::EmuRequestLeds);
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

		const auto cmd = static_cast<mqLib::SysexCommand>(_msg[4]);

		switch (cmd)
		{
		case mqLib::SysexCommand::EmuLCD:
			processLCDUpdate(_msg);
			break;
		case mqLib::SysexCommand::EmuLCDCGRata:
			processLCDCGRamUpdate(_msg);
			break;
		case mqLib::SysexCommand::EmuLEDs:
			processLedUpdate(_msg);
			break;
		case mqLib::SysexCommand::EmuRotaries:
		case mqLib::SysexCommand::EmuButtons:
		default:
			break;
		}
	}

	void FrontPanel::processLCDUpdate(const std::vector<uint8_t>& _msg) const
	{
		const auto* data = &_msg[5];

		std::array<uint8_t, 40> d{};

		for(size_t i=0; i<d.size(); ++i)
			d[i] = data[i];

		m_lcd->setText(d);
	}

	void FrontPanel::processLCDCGRamUpdate(const std::vector<uint8_t>& _msg) const
	{
		const auto *data = &_msg[5];

		std::array<uint8_t, 64> d{};
		for (size_t i = 0; i < d.size(); ++i)
			d[i] = data[i];

		m_lcd->setCgRam(d);
	}

	void FrontPanel::processLedUpdate(const std::vector<uint8_t>& _msg) const
	{
		const uint32_t leds = 
			(static_cast<uint32_t>(_msg[5]) << 24) |
			(static_cast<uint32_t>(_msg[6]) << 16) |
			(static_cast<uint32_t>(_msg[7]) << 8) |
			static_cast<uint32_t>(_msg[8]);

		for(size_t i=0; i<static_cast<uint32_t>(mqLib::Leds::Led::Count); ++i)
		{
			if(m_leds[i])
				m_leds[i]->setToggleState((leds & (1<<i)) != 0, juce::dontSendNotification);
		}
	}

	void FrontPanel::onButtonStateChanged(uint32_t _index) const
	{
		const auto* b = m_buttons[_index];
		
		std::map<pluginLib::MidiDataType, uint8_t> params;

		params[pluginLib::MidiDataType::ParameterIndex] = static_cast<uint8_t>(_index);

		if(b->getClickingTogglesState())
			params[pluginLib::MidiDataType::ParameterValue] = b->getToggleState() ? 1 : 0;
		else
			params[pluginLib::MidiDataType::ParameterValue] = b->getState() == juce::Button::buttonDown ? 1 : 0;

		m_controller.sendSysEx(Controller::EmuSendButton, params);
	}

	void FrontPanel::onEncoderValueChanged(uint32_t _index)
	{
		const auto* e = m_encoders[_index];

		float& vOld = m_encoderValues[_index];
		const auto v = static_cast<float>(e->getValue());

		auto delta = v - vOld;

		if(v >= 9.0f && vOld <= 1.0f)
			delta = v - (vOld + 10.0f);
		if(v <= 1.0f && vOld >= 9.0f)
			delta = v - (vOld - 10.0f);

		const int deltaInt = dsp56k::clamp(juce::roundToInt(g_encoderSpeed * delta) + 64, 0, 127);

		if(deltaInt == 64)
			return;

		vOld = v;

		std::map<pluginLib::MidiDataType, uint8_t> params;

		params[pluginLib::MidiDataType::ParameterIndex] = static_cast<uint8_t>(_index);
		params[pluginLib::MidiDataType::ParameterValue] = static_cast<uint8_t>(deltaInt);

		m_controller.sendSysEx(Controller::EmuSendRotary, params);
	}
}