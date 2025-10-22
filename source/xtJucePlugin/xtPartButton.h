#pragma once

#include "jucePluginEditorLib/partbutton.h"

namespace xtJucePlugin
{
	class Editor;

	class PartButton : public jucePluginEditorLib::PartButton
	{
	public:
		PartButton(Rml::Element* _button, Editor& _editor);

		void onClick(Rml::Event&) override;

		bool canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source) override;

	private:
		Editor& m_editor;
	};
}
