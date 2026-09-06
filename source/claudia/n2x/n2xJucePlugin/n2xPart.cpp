#include "n2xPart.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	Part::Part(Rml::Element* _button, Editor& _editor) : PartButton(_button, _editor), m_editor(_editor)
	{
	}

	void Part::onClick(Rml::Event& e)
	{
		if(!m_editor.getN2xController().setCurrentPart(getPart()))
		{
			juce::MessageManager::callAsync([this]
			{
				// FIXME: why?
				setChecked(true);
			});
		}
	}
}
