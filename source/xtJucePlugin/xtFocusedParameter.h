#pragma once

#include "jucePluginEditorLib/focusedParameter.h"

namespace xtJucePlugin
{
	class Editor;

	class FocusedParameter : public jucePluginEditorLib::FocusedParameter
	{
	public:
		FocusedParameter(const pluginLib::Controller& _controller, const pluginLib::ParameterBinding& _parameterBinding, const Editor& _editor);

		void updateParameter(const std::string& _name, const std::string& _value) override;
	private:
		const Editor& m_editor;
	};
}