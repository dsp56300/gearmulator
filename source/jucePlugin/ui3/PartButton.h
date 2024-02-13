#pragma once

#include "PatchManager.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include "../../juceUiLib/button.h"

namespace genericVirusUI
{
	class VirusEditor;

	class PartButton final : public genericUI::Button<juce::TextButton>, public juce::DragAndDropTarget
	{
	public:
		explicit PartButton(VirusEditor& _editor);

		void initalize(uint8_t _part);

		bool isInterestedInDragSource(const SourceDetails& _dragSourceDetails) override;
		void itemDropped(const SourceDetails& _dragSourceDetails) override;
		void paint(juce::Graphics& g) override;

		void itemDragEnter(const SourceDetails& _dragSourceDetails) override;
		void itemDragExit(const SourceDetails& _dragSourceDetails) override;

		void mouseDrag(const juce::MouseEvent& _event) override;

		void mouseDown(const juce::MouseEvent& _event) override;
		void mouseUp(const juce::MouseEvent& _event) override;
		void mouseExit(const juce::MouseEvent& event) override;

	private:
		void setIsDragTarget(bool _isDragTarget);
		void selectPreset(uint8_t _part) const;
		static std::pair<pluginLib::patchDB::PatchPtr, jucePluginEditorLib::patchManager::List*> getPatchFromDragSource(const SourceDetails& _source);

		VirusEditor& m_editor;
		bool m_isDragTarget = false;
		uint8_t m_part = 0;
		bool m_leftMouseDown = false;
	};
}
