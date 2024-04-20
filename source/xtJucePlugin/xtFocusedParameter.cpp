#include "xtFocusedParameter.h"

#include "xtEditor.h"
#include "xtLcd.h"

namespace xtJucePlugin
{
	FocusedParameter::FocusedParameter(const pluginLib::Controller& _controller, const pluginLib::ParameterBinding& _parameterBinding, const Editor& _editor)
		: jucePluginEditorLib::FocusedParameter(_controller, _parameterBinding, _editor)
		, m_editor(_editor)
	{
	}

	void FocusedParameter::updateParameter(const std::string& _name, const std::string& _value)
	{
		auto* lcd = m_editor.getLcd();
		if(!lcd)
			return;

		lcd->setCurrentParameter(_name, _value);
	}
}
