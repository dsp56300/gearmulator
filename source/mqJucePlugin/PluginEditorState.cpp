#include "PluginEditorState.h"

#include "mqEditor.h"
#include "PluginProcessor.h"

#include "jucePluginEditorLib/midiPorts.h"

#include "skins.h"

namespace mqJucePlugin
{
	PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor) : jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), g_includedSkins)
	{
		loadDefaultSkin();
	}

	void PluginEditorState::initContextMenu(juceRmlUi::Menu& _menu)
	{
		jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);

		auto& p = m_processor;

		const auto gain = static_cast<int>(std::roundf(p.getOutputGain()));

		juceRmlUi::Menu gainMenu;

		gainMenu.addEntry("0 dB (default)", true, gain == 1, [&p] { p.setOutputGain(1); });
		gainMenu.addEntry("+6 dB", true, gain == 2, [&p] { p.setOutputGain(2); });
		gainMenu.addEntry("+12 dB", true, gain == 4, [&p] { p.setOutputGain(4); });

		_menu.addSubMenu("Output Gain", std::move(gainMenu));

		jucePluginEditorLib::MidiPorts::createMidiPortsMenu(_menu, p.getMidiPorts());
	}

	bool PluginEditorState::initAdvancedContextMenu(juceRmlUi::Menu& _menu, bool _enabled)
	{
		jucePluginEditorLib::PluginEditorState::initAdvancedContextMenu(_menu, _enabled);

		const auto percent = m_processor.getDspClockPercent();
		const auto hz = m_processor.getDspClockHz();

		juceRmlUi::Menu clockMenu;

		auto makeEntry = [&](const int _percent)
		{
			const auto mhz = hz * _percent / 100 / 1000000;
			std::stringstream ss;
			ss << _percent << "% (" << mhz << " MHz)";
			if(_percent == 100)
				ss << " (Default)";
			clockMenu.addEntry(ss.str(), _enabled, percent == _percent, [this, _percent] { m_processor.setDspClockPercent(_percent); });
		};

		makeEntry(50);
		makeEntry(75);
		makeEntry(100);
		makeEntry(125);
		makeEntry(150);
		makeEntry(200);

		_menu.addSubMenu("DSP Clock", std::move(clockMenu));

		return true;
	}

	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new mqJucePlugin::Editor(m_processor, _skin);
	}
}