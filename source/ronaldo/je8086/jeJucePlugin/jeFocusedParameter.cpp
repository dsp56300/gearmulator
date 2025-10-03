#include "jeFocusedParameter.h"

#include "jeEditor.h"
#include "jeLcd.h"
#include "jeController.h"

namespace jeJucePlugin
{
	JeFocusedParameter::JeFocusedParameter(Editor& _editor, JeLcd& _lcd)
	: FocusedParameter(_editor.geJeController(), _editor)
	, m_editor(_editor)
	, m_lcd(_lcd)
	{
	}

	void JeFocusedParameter::updateParameter(const std::string& _name, const std::string& _value)
	{
		FocusedParameter::updateParameter(_name, _value);

		m_lcd.setParameterDisplay(_name, _value);
	}
}
