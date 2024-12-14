#include "VirusEditorState.h"

#include "VirusProcessor.h"

#include "VirusEditor.h"

namespace virus
{
	VirusEditorState::VirusEditorState(VirusProcessor& _processor, pluginLib::Controller& _controller, const std::vector<jucePluginEditorLib::Skin>& _includedSkins)
		: jucePluginEditorLib::PluginEditorState(_processor, _controller, _includedSkins)
	{
		loadDefaultSkin();
	}

	jucePluginEditorLib::Editor* VirusEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new genericVirusUI::VirusEditor(m_parameterBinding, static_cast<VirusProcessor&>(m_processor), _skin);
	}

	void VirusEditorState::initContextMenu(juce::PopupMenu& _menu)
	{
		jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);
		auto& p = m_processor;

		{
			juce::PopupMenu gainMenu;

			const auto gain = m_processor.getOutputGain();

			gainMenu.addItem("-12 dB", true, gain == 0.25f, [&p] { p.setOutputGain(0.25f); });
			gainMenu.addItem("-6 dB", true, gain == 0.5f, [&p] { p.setOutputGain(0.5f); });
			gainMenu.addItem("0 dB (default)", true, gain == 1, [&p] { p.setOutputGain(1); });
			gainMenu.addItem("+6 dB", true, gain == 2, [&p] { p.setOutputGain(2); });
			gainMenu.addItem("+12 dB", true, gain == 4, [&p] { p.setOutputGain(4); });

			_menu.addSubMenu("Output Gain", gainMenu);
		}

		if(const auto* editor = dynamic_cast<genericVirusUI::VirusEditor*>(getEditor()))
		{
			const auto& leds = editor->getLeds();

			if(leds && leds->supportsLogoAnimation())
			{
				_menu.addItem("Enable Logo Animation", true, leds->isLogoAnimationEnabled(), [this]
				{
					const auto* editor = dynamic_cast<genericVirusUI::VirusEditor*>(getEditor());
					if(editor)
					{
						const auto& leds = editor->getLeds();
						leds->toggleLogoAnimation();
					}
				});
			}
		}
	}

	bool VirusEditorState::initAdvancedContextMenu(juce::PopupMenu& _menu, bool _enabled)
	{
		jucePluginEditorLib::PluginEditorState::initAdvancedContextMenu(_menu, _enabled);

		const auto percent = m_processor.getDspClockPercent();
		const auto hz = m_processor.getDspClockHz();

		juce::PopupMenu clockMenu;

		auto makeEntry = [&](const int _percent)
		{
			const auto mhz = hz * _percent / 100 / 1000000;
			std::stringstream ss;
			ss << _percent << "% (" << mhz << " MHz)";
			if(_percent == 100)
				ss << " (Default)";
			clockMenu.addItem(ss.str(), _enabled, percent == _percent, [this, _percent] { m_processor.setDspClockPercent(_percent); });
		};

		makeEntry(50);
		makeEntry(75);
		makeEntry(100);
		makeEntry(125);
		makeEntry(150);
		makeEntry(200);

		_menu.addSubMenu("DSP Clock", clockMenu);

		const auto samplerates = m_processor.getDeviceSupportedSamplerates();

		if(samplerates.size() > 1)
		{
			juce::PopupMenu srMenu;

			const auto current = m_processor.getPreferredDeviceSamplerate();

			const auto preferred = m_processor.getDevicePreferredSamplerates();

			srMenu.addItem("Automatic (Match with host)", true, current == 0.0f, [this] { m_processor.setPreferredDeviceSamplerate(0.0f); });
			srMenu.addSeparator();
			srMenu.addSectionHeader("Official, used automatically");

			auto addSRs = [&](bool _usePreferred)
			{
				for (const float samplerate : samplerates)
				{
					const auto isPreferred = std::find(preferred.begin(), preferred.end(), samplerate) != preferred.end();

					if(isPreferred != _usePreferred)
						continue;

					const auto title = std::to_string(static_cast<int>(std::floor(samplerate + 0.5f))) + " Hz";

					srMenu.addItem(title, _enabled, std::fabs(samplerate - current) < 1.0f, [this, samplerate] { m_processor.setPreferredDeviceSamplerate(samplerate); });
				}
			};

			addSRs(true);
			srMenu.addSeparator();
			srMenu.addSectionHeader("Undocumented, use with care");
			addSRs(false);

			_menu.addSubMenu("Device Samplerate", srMenu);
		}

		return true;
	}
}