#pragma once

#include "jucePluginLib/parameterlistener.h"

namespace juce
{
	class Button;
}

namespace n2xJucePlugin
{
	class Editor;

	class OctLed
	{
	public:
		explicit OctLed(Editor& _editor);

	private:
		void bind();
		void updateStateFromParameter(const pluginLib::Parameter* _parameter) const;

		Editor& m_editor;
		juce::Button* m_octLed;
		pluginLib::ParameterListener m_onParamChanged;
		pluginLib::EventListener<uint8_t> m_onCurrentPartChanged;
		pluginLib::Parameter* m_param = nullptr;
	};
}
