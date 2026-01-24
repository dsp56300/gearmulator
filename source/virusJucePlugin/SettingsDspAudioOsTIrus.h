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

namespace virus
{
	class VirusProcessor;
}

namespace genericVirusUI
{
	class VirusEditor;

	class SettingsDspAudioOsTIrus : public jucePluginEditorLib::SettingsDeviceSpecific
	{
	public:
		SettingsDspAudioOsTIrus(const VirusEditor* _editor, Rml::Element* _root);

	private:
		void setupSamplerateButtons(Rml::Element* _template, const std::vector<float>& _samplerates, const std::vector<float>& _preferred, float _current, bool _usePreferred, virus::VirusProcessor& _processor);
		void updateButtons() const;

		const VirusEditor* m_editor;
		std::vector<std::pair<float, juceRmlUi::ElemButton*>> m_samplerateButtons;
	};
}

