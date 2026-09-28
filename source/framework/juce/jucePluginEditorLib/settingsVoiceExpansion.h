#pragma once

#include "settingsDeviceSpecific.h"

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class Editor;

	// The checkbox btVoiceExpansion of a device specific settings template, it switches the voice expansion of the
	// processor
	class SettingsVoiceExpansion : public SettingsDeviceSpecific
	{
	public:
		SettingsVoiceExpansion(Editor& _editor, Rml::Element* _root);
	};
}
