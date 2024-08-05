#pragma once

#include "jucePluginLib/parameterlistener.h"

namespace juce
{
	class Button;
}

namespace n2xJucePlugin
{
	class Editor;

	class Arp
	{
	public:
		explicit Arp(Editor& _editor);

	private:
		void bind();
		void updateStateFromParameter(const pluginLib::Parameter* _parameter) const;

		Editor& m_editor;
		juce::Button* m_btArpActive;
		pluginLib::ParameterListener m_onParamChanged;
		pluginLib::EventListener<uint8_t> m_onCurrentPartChanged;
		pluginLib::Parameter* m_param = nullptr;
	};
}
