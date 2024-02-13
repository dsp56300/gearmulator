#include "PluginEditorState.h"

#include "PluginProcessor.h"

#include "ui3/VirusEditor.h"

#include "../synthLib/os.h"

const std::vector<PluginEditorState::Skin> g_includedSkins =
{
	{"TI Trancy", "VirusTI_Trancy.json", ""}
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
	auto& p = m_processor;

	{
		juce::PopupMenu gainMenu;

		const auto gain = m_processor.getOutputGain();

		gainMenu.addItem("-12 db", true, gain == 0.25f, [&p] { p.setOutputGain(0.25f); });
		gainMenu.addItem("-6 db", true, gain == 0.5f, [&p] { p.setOutputGain(0.5f); });
		gainMenu.addItem("0 db (default)", true, gain == 1, [&p] { p.setOutputGain(1); });
		gainMenu.addItem("+6 db", true, gain == 2, [&p] { p.setOutputGain(2); });
		gainMenu.addItem("+12 db", true, gain == 4, [&p] { p.setOutputGain(4); });

		_menu.addSubMenu("Output Gain", gainMenu);
	}
}

void PluginEditorState::initAdvancedContextMenu(juce::PopupMenu& _menu, bool _enabled)
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
}
