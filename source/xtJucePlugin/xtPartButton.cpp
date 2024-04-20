#include "xtPartButton.h"

#include "xtEditor.h"

namespace xtJucePlugin
{
	PartButton::PartButton(Editor& _editor, const std::string& _name, ButtonStyle _buttonStyle)
		: jucePluginEditorLib::PartButton<DrawableButton>(_editor, _name, _buttonStyle)
		, m_editor(_editor)
	{
	}

	void PartButton::onClick()
	{
		m_editor.getParts().selectPart(getPart());
	}
}
