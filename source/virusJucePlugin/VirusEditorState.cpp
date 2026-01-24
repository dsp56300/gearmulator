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
		return new genericVirusUI::VirusEditor(static_cast<VirusProcessor&>(m_processor), _skin);
	}

	bool VirusEditorState::initAdvancedContextMenu(juceRmlUi::Menu& _menu, bool _enabled)
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
}