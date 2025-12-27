#include "jePartButton.h"

#include "jeTypes.h"
#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"

namespace jeJucePlugin
{
	JePartButton::JePartButton(Rml::Element* _button, jucePluginEditorLib::Editor& _editor): PartButton(_button, _editor)
	{
	}

	bool JePartButton::isPerformance(uint8_t _part)
	{
		return _part == static_cast<uint8_t>(JePart::Performance);
	}

	bool JePartButton::isPerformance() const
	{
		return isPerformance(getPart());
	}

	bool JePartButton::canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source)
	{
		if (!PartButton::canDrop(_event, _source))
			return false;

		const auto* savePatchDesc = jucePluginEditorLib::patchManager::SavePatchDesc::fromDragSource(*_source);

		if (!savePatchDesc->isPartValid())
			return true;

		return isPerformance() == isPerformance(static_cast<uint8_t>(savePatchDesc->getPart()));
	}
}
