#pragma once

#include "jucePluginEditorLib/partbutton.h"

namespace n2xJucePlugin
{
	class Editor;

	class Part : public jucePluginEditorLib::PartButton<juce::DrawableButton>
	{
	public:
		Part(Editor& _editor, const std::string& _name, ButtonStyle _buttonStyle);

		void onClick() override;
	private:
		Editor& m_editor;
	};
}
