#pragma once

#include "jucePluginLib/parameterbinding.h"

namespace mqJucePlugin
{
	class mqPartButton;
	class Controller;
	class Editor;

	class mqPartSelect
	{
	public:
		explicit mqPartSelect(mqJucePlugin::Editor& _editor, Controller& _controller, pluginLib::ParameterBinding& _parameterBinding);

		void onPlayModeChanged() const;

		void selectPart(uint8_t _index) const;

	private:
		void updateUiState() const;

		struct Part
		{
			mqPartButton* button = nullptr;
			juce::Button* led = nullptr;
		};

		mqJucePlugin::Editor& m_editor;
		Controller& m_controller;
		pluginLib::ParameterBinding& m_parameterBinding;
		std::array<Part, 16> m_parts{};
	};
}
