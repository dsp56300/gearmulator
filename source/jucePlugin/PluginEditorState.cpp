#include "PluginEditorState.h"

#include "PluginProcessor.h"

#include "ui3/VirusEditor.h"

#include "../synthLib/os.h"

const std::vector<PluginEditorState::Skin> g_includedSkins =
{
	{"Hoverland", "VirusC_Hoverland.json", ""},
	{"Trancy", "VirusC_Trancy.json", ""},
	{"Galaxpel", "VirusC_Galaxpel.json", ""}
};

PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor, pluginLib::Controller& _controller) : jucePluginEditorLib::PluginEditorState(_processor, _controller, g_includedSkins)
{
	loadDefaultSkin();
}

genericUI::Editor* PluginEditorState::createEditor(const Skin& _skin, std::function<void()> _openMenuCallback)
{
	return new genericVirusUI::VirusEditor(m_parameterBinding, static_cast<AudioPluginAudioProcessor&>(m_processor), _skin.jsonFilename, _skin.folder, _openMenuCallback);
}

void PluginEditorState::initContextMenu(juce::PopupMenu& _menu)
{
	jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);

	juce::PopupMenu gainMenu;

	auto& p = m_processor;

	const auto gain = m_processor.getOutputGain();

	gainMenu.addItem("-12 db", true, gain == 0.25f, [&p] { p.setOutputGain(0.25f); });
	gainMenu.addItem("-6 db", true, gain == 0.5f, [&p] { p.setOutputGain(0.5f); });
	gainMenu.addItem("0 db (default)", true, gain == 1, [&p] { p.setOutputGain(1); });
	gainMenu.addItem("+6 db", true, gain == 2, [&p] { p.setOutputGain(2); });
	gainMenu.addItem("+12 db", true, gain == 4, [&p] { p.setOutputGain(4); });

	_menu.addSubMenu("Output Gain", gainMenu);
}
