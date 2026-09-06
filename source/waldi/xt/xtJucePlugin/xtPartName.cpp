#include "xtPartName.h"

#include "xtController.h"
#include "xtEditor.h"

namespace xtJucePlugin
{
	PartName::PartName(Rml::Element* _button, Editor& _editor)
	: PartButton(_button, _editor)
	, m_editor(_editor)
	, m_onProgramChanged(_editor.getXtController().onProgramChanged)
	, m_onPlayModeChanged(_editor.getXtController().onPlayModeChanged)
	{
		m_onPlayModeChanged = [this](const bool&)
		{
			updatePartName();
		};

		m_onProgramChanged = [this](uint8_t)
		{
			updatePartName();
		};
	}

	bool PartName::canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source)
	{
		if(getPart() > 0 && !m_editor.getXtController().isMultiMode())
			return false;
		return PartButton::canDrop(_event, _source);
	}
/*
	void PartName::mouseDrag(const juce::MouseEvent& _event)
	{
		if(getPart() > 0 && !m_editor.getXtController().isMultiMode())
			return;
		PartButton<TextButton>::mouseDrag(_event);
	}
*/

	void PartName::updatePartName() const
	{
		const auto& c = m_editor.getXtController();

		if(getPart() > 0 && !c.isMultiMode())
		{
			getElement()->SetInnerRML("-");
		}
		else
		{
			const auto name = c.getSingleName(getPart());
			getElement()->SetInnerRML(Rml::StringUtilities::EncodeRml(name));
		}
	}
}
