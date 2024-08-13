#pragma once

#include <string>
#include <set>

namespace pluginLib
{
	class ParameterBinding;
}

namespace juce
{
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
		juce::Button* m_btVmMap;
		bool m_enabled = false;
	};
}
