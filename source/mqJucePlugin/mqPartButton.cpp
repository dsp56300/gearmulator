#include "mqPartButton.h"
#include "mqEditor.h"
#include "mqPartSelect.h"

namespace mqJucePlugin
{
	mqPartButton::mqPartButton(mqJucePlugin::Editor& _editor, const std::string& _name, ButtonStyle _buttonStyle)
	: PartButton(_editor, _name, _buttonStyle)
	, m_mqEditor(_editor)
	{
	}

	bool mqPartButton::isInterestedInDragSource(const SourceDetails& dragSourceDetails)
	{
		return PartButton<DrawableButton>::isInterestedInDragSource(dragSourceDetails);
	}

	void mqPartButton::onClick()
	{
		m_mqEditor.getPartSelect()->selectPart(getPart());
	}
}
