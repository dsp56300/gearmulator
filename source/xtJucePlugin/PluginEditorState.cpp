#include "PluginEditorState.h"

#include "xtEditor.h"
#include "PluginProcessor.h"

#include "skins.h"

#include "weGraph.h"

namespace xtJucePlugin
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
	}

	bool PluginEditorState::initAdvancedContextMenu(juce::PopupMenu& _menu, bool _enabled)
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

		return true;
	}

	void PluginEditorState::openMenu(const juce::MouseEvent* _event)
	{
		if (dynamic_cast<Graph*>(_event->eventComponent))
			return;
		jucePluginEditorLib::PluginEditorState::openMenu(_event);
	}


	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new xtJucePlugin::Editor(m_processor, m_parameterBinding, _skin);
	}
}