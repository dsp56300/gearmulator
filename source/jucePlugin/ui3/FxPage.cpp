#include "FxPage.h"

#include "VirusEditor.h"

#include "../ParameterNames.h"
#include "../VirusController.h"

namespace genericVirusUI
{
	FxPage::FxPage(VirusEditor& _editor) : m_editor(_editor)
	{
		m_reverbContainer = _editor.findComponent("ContainerReverb");
		m_delayContainer = _editor.findComponent("ContainerDelay");

		const auto delayReverbMode = m_editor.getController().getParameterIndexByName(Virus::g_paramDelayReverbMode);
		const auto p = m_editor.getController().getParameter(delayReverbMode, 0);

		if (p)
		{
			p->getValueObject().addListener(this);
		}

		updateReverbDelay();
	}

	FxPage::~FxPage()
	{
		const auto delayReverbMode = m_editor.getController().getParameterIndexByName(Virus::g_paramDelayReverbMode);
		const auto p = m_editor.getController().getParameter(delayReverbMode, 0);
		if(p)
			p->getValueObject().removeListener(this);
	}

	void FxPage::valueChanged(juce::Value& value)
	{
		updateReverbDelay();
	}

	void FxPage::updateReverbDelay() const
	{
		const auto delayReverbMode = m_editor.getController().getParameterIndexByName(Virus::g_paramDelayReverbMode);
		const auto p = m_editor.getController().getParameter(delayReverbMode, 0);

		if (!p)
			return;

		const auto value = static_cast<int>(p->getValueObject().getValueSource().getValue());

		const bool isReverb = (value > 1 && value < 5);

		VirusEditor::setEnabled(*m_delayContainer, !isReverb);
		VirusEditor::setEnabled(*m_reverbContainer, isReverb);
	}
}
