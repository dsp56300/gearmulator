#pragma once

#include "jucePluginEditorLib/settingsDeviceSpecific.h"

namespace Rml
{
	class Element;
}

namespace genericVirusUI
{
	class SettingsGuiOsTIrus : public jucePluginEditorLib::SettingsDeviceSpecific
	{
	public:
		SettingsGuiOsTIrus(Rml::Element* _root);
	};
}
