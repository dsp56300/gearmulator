#include "n2xPluginEditorState.h"

#include "n2xEditor.h"
#include "n2xPluginProcessor.h"

#include "jucePluginEditorLib/midiPorts.h"

#include "synthLib/os.h"

#include "skins.h"

namespace n2xJucePlugin
{
	PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor) : jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), g_includedSkins)
	{
		loadDefaultSkin();
	}

	void PluginEditorState::initContextMenu(juce::PopupMenu& _menu)
	{
		jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);

		auto& p = m_processor;

		const auto gain = static_cast<int>(std::roundf(p.getOutputGain()));

		juce::PopupMenu gainMenu;

		gainMenu.addItem("0 dB (default)", true, gain == 1, [&p] { p.setOutputGain(1); });
		gainMenu.addItem("+6 dB", true, gain == 2, [&p] { p.setOutputGain(2); });
		gainMenu.addItem("+12 dB", true, gain == 4, [&p] { p.setOutputGain(4); });

		_menu.addSubMenu("Output Gain", gainMenu);

		jucePluginEditorLib::MidiPorts::createMidiPortsMenu(_menu, p.getMidiPorts());
	}

	bool PluginEditorState::initAdvancedContextMenu(juce::PopupMenu& _menu, const bool _enabled)
	{
		return jucePluginEditorLib::PluginEditorState::initAdvancedContextMenu(_menu, _enabled);
	}

	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new n2xJucePlugin::Editor(m_processor, m_parameterBinding, _skin);
	}
}
