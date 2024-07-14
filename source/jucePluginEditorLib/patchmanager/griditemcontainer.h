#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class Grid;

	class GridItemContainer : public juce::Component
	{
	public:
		static constexpr uint32_t InvalidItem = ~0;

		GridItemContainer(Grid& _grid);

		void mouseDown(const juce::MouseEvent& _e) override;
		juce::ScaledImage createSnapshotOfRows(const juce::SparseSet<int>& _rows, int& _x, int& _y) const;
		void mouseDrag(const juce::MouseEvent& _e) override;
		void mouseUp(const juce::MouseEvent& _e) override;

	private:
		uint32_t mouseToItemIndex(const juce::MouseEvent& _e) const;

		Grid& m_grid;
		uint32_t m_itemIndexMouseDown = InvalidItem;
		bool m_isDragging = false;
	};
}
