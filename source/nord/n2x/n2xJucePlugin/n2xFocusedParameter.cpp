#include "n2xFocusedParameter.h"

#include "n2xEditor.h"
#include "n2xController.h"

namespace n2xJucePlugin
{
	FocusedParameter::FocusedParameter(const Editor& _editor)
	: jucePluginEditorLib::FocusedParameter(_editor.getN2xController(), _editor.getParameterBinding(), _editor)
	, m_editor(_editor)
	{
	}

	void FocusedParameter::updateParameter(const std::string& _name, const std::string& _value)
	{
		// we only have 3 digits but some parameters have values <= -100, doesn't look nice at all
//		m_editor.getLCD().setOverrideText(_value);
		jucePluginEditorLib::FocusedParameter::updateParameter(_name, _value);
	}
}
