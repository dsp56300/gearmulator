#include "jePluginEditorState.h"

#include "jeEditor.h"
#include "jePluginProcessor.h"

#include "jucePluginEditorLib/midiPorts.h"

#include "skins.h"

namespace jeJucePlugin
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

	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new jeJucePlugin::Editor(m_processor, _skin);
	}
}
