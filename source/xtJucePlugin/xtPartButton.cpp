#include "xtPartButton.h"

#include "xtEditor.h"

namespace xtJucePlugin
{
	PartButton::PartButton(Editor& _editor, const std::string& _name, ButtonStyle _buttonStyle)
		: jucePluginEditorLib::PartButton<DrawableButton>(_editor, _name, _buttonStyle)
		, m_editor(_editor)
	{
	}

	void PartButton::setPart(const uint8_t _part)
	{
		m_part = _part;
	}

	void PartButton::onClick()
	{
		if(!m_editor.getParts().selectPart(m_part))
			setToggleState(false, juce::dontSendNotification);
	}
}
