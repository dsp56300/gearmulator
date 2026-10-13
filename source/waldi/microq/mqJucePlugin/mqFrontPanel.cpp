#include "mqFrontPanel.h"

#include <algorithm>

#include "mqEditor.h"
#include "mqLcd.h"
#include "mqLcdText.h"

#include "jucePluginEditorLib/frontPanel.h"

#include "juceRmlUi/rmlHelper.h"

namespace mqJucePlugin
{
	FrontPanel::FrontPanel(const Editor& _editor)
	{
		if (auto* lcdArea = _editor.findChild("lcdArea", false))
			m_lcd.reset(new MqLcd(lcdArea));

		std::array<Rml::Element*, 2> lcdLines{};

		lcdLines[0] = _editor.findChild("lcdLineA", false);
		lcdLines[1] = _editor.findChild("lcdLineB", lcdLines[0] != nullptr);

		if (lcdLines[0])
		{
			if (m_lcd)
			{
				juceRmlUi::helper::setVisible(lcdLines[0], false);
				juceRmlUi::helper::setVisible(lcdLines[1], false);
			}
			else
			{
				m_lcd.reset(new MqLcdText(*lcdLines[0], *lcdLines[1]));
			}
		}

		jucePluginEditorLib::FrontPanel::Binding binding;

		binding.lcdCols = 20;
		binding.lcdRows = 2;

		binding.lcdSink = [this](const std::vector<uint8_t>& _cells)
		{
			std::array<uint8_t, 40> text{};
			std::copy_n(_cells.begin(), text.size(), text.begin());
			if (m_lcd)
				m_lcd->setText(text);
		};

		binding.cgRamSink = [this](const std::array<uint8_t, 64>& _cgRam)
		{
			auto cgRam = _cgRam;
			if (m_lcd)
				m_lcd->setCgRam(cgRam);
		};

		binding.leds =
		{
			"ledOsc1", "ledFilter2", "ledInst1", "ledGlobal", "ledPlay",
			"ledOsc2", "ledAmpFxArp", "ledInst2", "ledMulti", "ledPeek",
			"ledOsc3", "ledEnv1", "ledInst3", "ledEdit", "ledMultimode",
			"ledMixerRouting", "ledEnv2", "ledInst4", "ledSound", "ledShift",
			"ledFilter1", "ledEnv3", "ledModMatrix", "ledLFOs", "ledEnv4",
			"ledPower"
		};

		binding.buttons =
		{
			"buttonInst1", "buttonInst2", "buttonInst3", "buttonInst4",
			"buttonD", "buttonL", "buttonR", "buttonU", "buttonGlobal",
			"buttonMulti", "buttonEdit", "buttonSound", "buttonShift",
			"buttonMultimode", "buttonPeek", "buttonPlay", "buttonPower"
		};

		binding.encoders =
		{
			"encoderMatrix4",	"encoderMatrix3",	"encoderMatrix2",	"encoderMatrix1",
			"encoderLCDRight",	"encoderLCDLeft",
			"encoderAlphaDial"
		};

		m_panel.reset(new jucePluginEditorLib::FrontPanel(_editor, std::move(binding)));
	}

	FrontPanel::~FrontPanel()
	{
		m_panel.reset();
		m_lcd.reset();
	}
}
