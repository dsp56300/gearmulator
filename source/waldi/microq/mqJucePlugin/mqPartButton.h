#pragma once

#include "jucePluginEditorLib/partbutton.h"

namespace mqJucePlugin
{
	class Editor;
	class mqPartSelect;

	class mqPartButton : public jucePluginEditorLib::PartButton
	{
	public:
		explicit mqPartButton(Rml::Element* _button, Editor& _editor);

		void onClick(Rml::Event&) override;

		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files) override;

	private:
		Editor& m_mqEditor;
	};
}
