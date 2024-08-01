#include "n2xPart.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	Part::Part(Editor& _editor, const std::string& _name, const ButtonStyle _buttonStyle) : PartButton(_editor, _name, _buttonStyle), m_editor(_editor)
	{
	}

	void Part::onClick()
	{
		m_editor.getN2xController().setCurrentPart(getPart());
	}
}
