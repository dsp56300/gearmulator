#pragma once

#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"
#include "jucePluginEditorLib/partbutton.h"

namespace jeJucePlugin
{
	class JePartButton final : public jucePluginEditorLib::PartButton
	{
	public:
		JePartButton(Rml::Element* _button, jucePluginEditorLib::Editor& _editor);

		static bool isPerformance(uint8_t _part);
		bool isPerformance() const;

		bool canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source) override;
	};
}
