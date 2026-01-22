#pragma once

#include "jucePluginEditorLib/settingsDeviceSpecific.h"

namespace Rml
{
	class Element;
}

namespace genericVirusUI
{
	class VirusEditor;

	class SettingsGuiOsTIrus : public jucePluginEditorLib::SettingsDeviceSpecific
	{
	public:
		SettingsGuiOsTIrus(const VirusEditor* _editor, Rml::Element* _root);
	};
}
