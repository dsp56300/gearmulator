#pragma once

#include "../jucePluginEditorLib/partbutton.h"

namespace xtJucePlugin
{
	class Editor;

	class PartButton : public jucePluginEditorLib::PartButton<juce::DrawableButton>
	{
	public:
		PartButton(Editor& _editor, const std::string& _name, ButtonStyle _buttonStyle);

		void setPart(uint8_t _part);

		void onClick() override;

	private:
		Editor& m_editor;
		uint8_t m_part;
	};
}
