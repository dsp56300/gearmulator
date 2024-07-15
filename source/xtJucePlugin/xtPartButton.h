#pragma once

#include "jucePluginEditorLib/partbutton.h"

namespace xtJucePlugin
{
	class Editor;

	class PartButton : public jucePluginEditorLib::PartButton<juce::DrawableButton>
	{
	public:
		PartButton(Editor& _editor, const std::string& _name, ButtonStyle _buttonStyle);

		void onClick() override;

		bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
		void mouseDrag(const juce::MouseEvent& _event) override;

	private:
		Editor& m_editor;
	};
}
