#pragma once

#include "../jucePluginLib/parameterbinding.h"

class Controller;

namespace mqJucePlugin
{
	class Editor;
}

class mqPartSelect
{
public:
	explicit mqPartSelect(const mqJucePlugin::Editor& _editor, Controller& _controller, pluginLib::ParameterBinding& _parameterBinding);

private:
	void updateUiState() const;
	void selectPart(uint8_t _index) const;

	struct Part
	{
		juce::Button* button = nullptr;
		juce::Button* led = nullptr;
	};

	Controller& m_controller;
	pluginLib::ParameterBinding& m_parameterBinding;
	std::array<Part, 16> m_parts{};
};

