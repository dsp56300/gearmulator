#pragma once

#include "jucePluginEditorLib/partbutton.h"

namespace genericVirusUI
{
	class VirusEditor;

	class PartButton final : public jucePluginEditorLib::PartButton
	{
	public:
		explicit PartButton(Rml::Element* _button, VirusEditor& _editor);

		bool canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source) override;

		void onClick(Rml::Event& _e) override;
		void setButtonText(const std::string& _text);

	private:
		void selectPreset(const Rml::Event& _event, uint8_t _part) const;

		VirusEditor& m_editor;
	};
}
