#include "xtPartButton.h"

#include "xtController.h"
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

	bool PartButton::isInterestedInDragSource(const SourceDetails& dragSourceDetails)
	{
		if(getPart() > 0 && !m_editor.getXtController().isMultiMode())
			return false;
		return jucePluginEditorLib::PartButton<DrawableButton>::isInterestedInDragSource(dragSourceDetails);
	}

	void PartButton::mouseDrag(const juce::MouseEvent& _event)
	{
		if(getPart() > 0 && !m_editor.getXtController().isMultiMode())
			return;
		jucePluginEditorLib::PartButton<DrawableButton>::mouseDrag(_event);
	}
}
