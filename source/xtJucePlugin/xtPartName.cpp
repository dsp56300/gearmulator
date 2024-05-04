#include "xtPartName.h"

#include "xtController.h"
#include "xtEditor.h"

namespace xtJucePlugin
{
	PartName::PartName(Editor& _editor)
	: jucePluginEditorLib::PartButton<juce::TextButton>(_editor)
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

	bool PartName::isInterestedInDragSource(const SourceDetails& _dragSourceDetails)
	{
		if(getPart() > 0 && !m_editor.getXtController().isMultiMode())
			return false;
		return PartButton<TextButton>::isInterestedInDragSource(_dragSourceDetails);
	}

	void PartName::mouseDrag(const juce::MouseEvent& _event)
	{
		if(getPart() > 0 && !m_editor.getXtController().isMultiMode())
			return;
		PartButton<TextButton>::mouseDrag(_event);
	}

	void PartName::updatePartName()
	{
		const auto& c = m_editor.getXtController();

		if(getPart() > 0 && !c.isMultiMode())
		{
			setButtonText("-");
		}
		else
		{
			const auto name = c.getSingleName(getPart());
			setButtonText(name);
		}
	}
}
