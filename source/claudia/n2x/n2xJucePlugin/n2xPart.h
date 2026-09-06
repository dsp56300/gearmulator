#pragma once

#include "jucePluginEditorLib/partbutton.h"

namespace n2xJucePlugin
{
	class Editor;

	class Part : public jucePluginEditorLib::PartButton
	{
	public:
		Part(Rml::Element* _button, Editor& _editor);

		void onClick(Rml::Event&) override;

	private:
		Editor& m_editor;
	};
}
