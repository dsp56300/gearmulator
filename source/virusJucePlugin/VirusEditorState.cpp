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
}