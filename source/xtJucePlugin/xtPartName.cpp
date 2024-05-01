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
		m_onPlayModeChanged = [this](const bool& _multiMode)
		{
			updatePartName();
		};

		m_onProgramChanged = [this](uint8_t _program)
		{
			updatePartName();
		};
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
