#include "settingsGui.h"

#include "pluginProcessor.h"
#include "pluginEditorState.h"

namespace jucePluginEditorLib
{
	void SettingsGui::createUi(Rml::Element* _root)
	{
		createToggleButton(_root, "btForceSoftwareRendering", "forceSoftwareRenderer", [this](bool)
		{
			juce::MessageManager::callAsync([this]
			{
				auto* editorState = m_processor.getEditorState();
				editorState->loadSkin(editorState->getCurrentSkin());
			});
		});
	}
}
