#pragma once

#include "jucePluginEditorLib/partbutton.h"

#include "baseLib/event.h"

namespace xtJucePlugin
{
	class Editor;

	class PartName : public jucePluginEditorLib::PartButton
	{
	public:
		explicit PartName(Rml::Element* _button, Editor& _editor);

		bool canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source) override;

	private:
		void updatePartName() const;

		Editor& m_editor;
		baseLib::EventListener<uint8_t> m_onProgramChanged;
		baseLib::EventListener<bool> m_onPlayModeChanged;
	};
}
