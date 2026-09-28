#include "settingsVoiceExpansion.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"

namespace jucePluginEditorLib
{
	SettingsVoiceExpansion::SettingsVoiceExpansion(Editor& _editor, Rml::Element* _root)
	{
		auto& processor = _editor.getProcessor();

		auto* btRoot = juceRmlUi::helper::findChild(_root, "btVoiceExpansion", false);
		if (!btRoot)
			return;

		auto* bt = juceRmlUi::helper::findChild(btRoot, "button");
		juceRmlUi::ElemButton::setChecked(bt, processor.isVoiceExpansionEnabled());

		juceRmlUi::EventListener::AddClick(btRoot, [bt, &processor]
		{
			const auto newState = !processor.isVoiceExpansionEnabled();
			juceRmlUi::ElemButton::setChecked(bt, newState);
			processor.setVoiceExpansion(newState);
		});
	}
}
