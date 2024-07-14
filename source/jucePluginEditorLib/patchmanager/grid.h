#pragma once

#include "griditemcontainer.h"
#include "gridviewport.h"
#include "listmodel.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class GridItem;

	class Grid : public ListModel, public juce::Component
	{
	public:
		Grid(PatchManager& _pm);

		void paint(juce::Graphics& g) override;

		int getItemWidth() const { return m_itemWidth; }
		int getItemHeight() const { return m_itemHeight; }

		void setVisibleItemRange(const std::pair<uint32_t, uint32_t>& _range);

		const GridViewport& getViewport() const { return m_viewport; }

		bool isSelected(const uint32_t _index)
		{
			return m_selectedItems.find(_index) != m_selectedItems.end();
		}

		void selectItem(uint32_t _index, bool _deselectOthers);
		void deselectItem(uint32_t _index);
		GridItem* getItem(uint32_t _index) const;

	private:
		void resized() override;
		int getNeededColumnCount();
		int getVisibleRowCount() const;
		void updateViewportSize();

	public:
		// ListModel
		juce::Colour findColor(int _colorId) override;
		const juce::LookAndFeel& getStyle() const override;
		void onModelChanged() override;
		void redraw() override;
		void ensureVisible(int _row) override;
		int getSelectedEntry() const override;
		juce::SparseSet<int> getSelectedEntries() const override;
		void deselectAll() override;
		void setSelectedEntries(const juce::SparseSet<int>&) override;
		juce::Rectangle<int> getEntryPosition(int _row, bool _relativeToComponentTopLeft) override;

	private:
		int m_itemHeight = 22;
		int m_itemWidth = 150;

		GridViewport m_viewport;
		GridItemContainer m_itemContainer;

		std::map<uint32_t, std::unique_ptr<GridItem>> m_items;
		std::list<std::unique_ptr<GridItem>> m_itemPool;
		std::set<uint32_t> m_selectedItems;
	};
}
