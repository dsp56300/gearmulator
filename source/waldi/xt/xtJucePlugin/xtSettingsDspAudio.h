#pragma once

#include "jucePluginEditorLib/settingsDeviceSpecific.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class Editor;
}

namespace xtJucePlugin
{
	class SettingsDspAudio : public jucePluginEditorLib::SettingsDeviceSpecific
	{
	public:
		SettingsDspAudio(jucePluginEditorLib::Editor& _editor, Rml::Element* _root);
	};
}
