#pragma once

#include <map>
#include <string>
#include <set>

namespace pluginLib
{
	class ParameterBinding;
}

namespace juce
{
	class Component;
	class Button;
}

namespace n2xJucePlugin
{
	class Editor;

	class VmMap
	{
	public:
		explicit VmMap(Editor& _editor, pluginLib::ParameterBinding& _binding);

	private:
		void toggleVmMap(bool _enabled);

		Editor& m_editor;
		pluginLib::ParameterBinding& m_binding;

		std::set<std::string> m_paramNames;
		std::map<std::string, juce::Component*> m_boundComponents;
		juce::Button* m_btVmMap;
		bool m_enabled = false;
	};
}
