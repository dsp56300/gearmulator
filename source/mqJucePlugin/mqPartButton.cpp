#include "mqPartButton.h"
#include "mqEditor.h"
#include "mqPartSelect.h"

namespace mqJucePlugin
{
	mqPartButton::mqPartButton(Rml::Element* _button, Editor& _editor)
	: PartButton(_button, _editor)
	, m_mqEditor(_editor)
	{
	}

	void mqPartButton::onClick(Rml::Event& _event)
	{
		m_mqEditor.getPartSelect()->selectPart(getPart());
	}
}
