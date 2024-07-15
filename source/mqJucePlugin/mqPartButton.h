#pragma once

#include "jucePluginEditorLib/partbutton.h"

class mqPartSelect;

namespace mqJucePlugin
{
	class Editor;
}

class mqPartButton : public jucePluginEditorLib::PartButton<juce::DrawableButton>
{
public:
	explicit mqPartButton(mqJucePlugin::Editor& _editor, const std::string& _name, ButtonStyle _buttonStyle);

	bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;

	void onClick() override;

private:
	mqJucePlugin::Editor& m_mqEditor;
};
