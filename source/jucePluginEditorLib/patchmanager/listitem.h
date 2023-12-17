#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class List;

	class ListItem : public juce::Component, public juce::DragAndDropTarget
	{
	public:
		explicit ListItem(List& _list);

		void paint(juce::Graphics& g) override;

		void itemDragEnter(const SourceDetails& dragSourceDetails) override;
		void itemDragExit(const SourceDetails& dragSourceDetails) override;
		void itemDragMove(const SourceDetails& dragSourceDetails) override;
		bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
		void itemDropped(const SourceDetails& dragSourceDetails) override;
		void mouseDown(const juce::MouseEvent& event) override;
		bool hitTest(int x, int y) override;
	private:

		void updateDragTypeFromPosition(const SourceDetails& dragSourceDetails);

		enum class DragType
		{
			Off,
			Above,
			Below
		};

		List& m_list;
		DragType m_drag = DragType::Off;
	};
}
