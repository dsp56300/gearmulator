#include "FxPage.h"

#include "VirusEditor.h"

namespace genericVirusUI
{
	FxPage::FxPage(VirusEditor& _editor) : m_editor(_editor)
	{
		m_reverbContainer = _editor.findComponent("ContainerReverb");
		m_delayContainer = _editor.findComponent("ContainerDelay");

		const auto p = m_editor.getController().getParameter(Virus::Param_DelayReverbMode, 0);

		if (p)
		{
			p->getValueObject().addListener(this);
		}

		updateReverbDelay();
	}

	FxPage::~FxPage()
	{
		const auto p = m_editor.getController().getParameter(Virus::Param_DelayReverbMode, 0);
		if(p)
			p->getValueObject().removeListener(this);
	}

	void FxPage::valueChanged(juce::Value& value)
	{
		updateReverbDelay();
	}

	void FxPage::updateReverbDelay() const
	{
		const auto p = m_editor.getController().getParameter(Virus::Param_DelayReverbMode, 0);

		if (!p)
			return;

		const auto value = static_cast<int>(p->getValueObject().getValueSource().getValue());

		const bool isReverb = (value > 1 && value < 5);

		m_delayContainer->setVisible(!isReverb);
		m_reverbContainer->setVisible(isReverb);
	}
}
