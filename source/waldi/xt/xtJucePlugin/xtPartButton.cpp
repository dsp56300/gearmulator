#include "xtPartButton.h"

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
}
