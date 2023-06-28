#include "PluginEditorState.h"

#include "mqEditor.h"
#include "PluginProcessor.h"

#include "../synthLib/os.h"

const std::vector<PluginEditorState::Skin> g_includedSkins =
{
	{"Editor", "mqDefault.json", ""},
	{"Device", "mqFrontPanel.json", ""}
};

PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor) : jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), g_includedSkins)
{
	loadDefaultSkin();
}

void PluginEditorState::initContextMenu(juce::PopupMenu& _menu)
{
	jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);

	auto& p = static_cast<AudioPluginAudioProcessor&>(m_processor);

	const auto gain = static_cast<int>(std::roundf(p.getOutputGain()));

	juce::PopupMenu gainMenu;

	gainMenu.addItem("0 db (default)", true, gain == 1, [&p] { p.setOutputGain(1); });
	gainMenu.addItem("+6 db", true, gain == 2, [&p] { p.setOutputGain(2); });
	gainMenu.addItem("+12 db", true, gain == 4, [&p] { p.setOutputGain(4); });

	_menu.addSubMenu("Output Gain", gainMenu);
}

genericUI::Editor* PluginEditorState::createEditor(const Skin& _skin, std::function<void()> _openMenuCallback)
{
	return new mqJucePlugin::Editor(m_processor, m_parameterBinding, _skin.folder, _skin.jsonFilename);
}
