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
		bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
		void itemDropped(const SourceDetails& dragSourceDetails) override;
		void mouseDown(const juce::MouseEvent& event) override;
		bool hitTest(int x, int y) override;
	private:
		List& m_list;
		bool m_drag = false;
	};
}
