#include "settingsSkin.h"

#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"

namespace jucePluginEditorLib
{
	SettingsSkin::SettingsSkin(Processor& _processor) : SettingsPlugin()
	{
	}
	void SettingsSkin::createUi(Rml::Element* _root)
	{
		auto* skinEntry = juceRmlUi::helper::findChild(_root, "entry");
	}
}