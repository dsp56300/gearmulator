#pragma once

#include "jucePluginEditorLib/partbutton.h"

namespace mqJucePlugin
{
	class Editor;
	class mqPartSelect;

	class mqPartButton : public jucePluginEditorLib::PartButton<juce::DrawableButton>
	{
	public:
		explicit mqPartButton(Editor& _editor, const std::string& _name, ButtonStyle _buttonStyle);

		bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;

		void onClick() override;

	private:
		Editor& m_mqEditor;
	};
}
