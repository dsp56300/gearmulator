#include "xtPartButton.h"

#include "xtController.h"
#include "xtEditor.h"

namespace xtJucePlugin
{
	PartButton::PartButton(Rml::Element* _element, Editor& _editor)
		: jucePluginEditorLib::PartButton(_element, _editor)
		, m_editor(_editor)
	{
	}

	void PartButton::onClick(Rml::Event&)
	{
		m_editor.getParts().selectPart(getPart());
	}

	bool PartButton::canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source)
	{
		if(getPart() > 0 && !m_editor.getXtController().isMultiMode())
			return false;
		return jucePluginEditorLib::PartButton::canDrop(_event, _source);
	}
/*
	void PartButton::mouseDrag(const juce::MouseEvent& _event)
	{
		if(getPart() > 0 && !m_editor.getXtController().isMultiMode())
			return;
		jucePluginEditorLib::PartButton<DrawableButton>::mouseDrag(_event);
	}
*/
}
