#pragma once

#include "PatchManager.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include "../../jucePluginEditorLib/partbutton.h"

namespace genericVirusUI
{
	class VirusEditor;

	class PartButton final : public jucePluginEditorLib::PartButton<juce::TextButton>
	{
	public:
		explicit PartButton(VirusEditor& _editor);

		bool isInterestedInDragSource(const SourceDetails& _dragSourceDetails) override;

		void paint(juce::Graphics& g) override;

		void onClick() override;
	private:
		void selectPreset(uint8_t _part) const;

		VirusEditor& m_editor;
	};
}
