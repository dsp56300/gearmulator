#pragma once

#include "jucePluginEditorLib/settingsDeviceSpecific.h"

#include <vector>

namespace Rml
{
	class Element;
}

namespace juceRmlUi
{
	class ElemButton;
}

namespace genericVirusUI
{
	class VirusEditor;

	class SettingsDspAudioOsTIrus : public jucePluginEditorLib::SettingsDeviceSpecific
	{
	public:
		SettingsDspAudioOsTIrus(const VirusEditor* _editor, Rml::Element* _root);

	private:
		void updateButtons() const;

		const VirusEditor* m_editor;
		std::vector<std::pair<float, juceRmlUi::ElemButton*>> m_samplerateButtons;
	};
}

