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
		static constexpr uint32_t InvalidItem = ~0;

		Grid(PatchManager& _pm);
		~Grid() override;

		Grid(Grid&&) = delete;
		Grid(const Grid&) = delete;
		Grid& operator = (Grid&&) = delete;
		Grid& operator = (const Grid&) = delete;

		void paint(juce::Graphics& g) override;

		auto getItemWidth() const { return m_itemWidth; }
		auto getItemHeight() const { return m_itemHeight; }
		auto getSuggestedItemsPerRow() const { return m_suggestedItemsPerRow; }

		void setItemWidth(float _width);
		void setItemHeight(float _height);
		void setSuggestedItemsPerRow(uint32_t _count);

		void setVisibleItemRange(const std::pair<uint32_t, uint32_t>& _range);

		const GridViewport& getViewport() const { return m_viewport; }

		bool isSelected(const uint32_t _index)
		{
			return m_selectedItems.find(_index) != m_selectedItems.end();
		}

		void selectItem(uint32_t _index, bool _deselectOthers);
		void deselectItem(uint32_t _index);
		GridItem* getItem(uint32_t _index) const;
		void selectRange(uint32_t _newIndex);
		bool keyPressed(const juce::KeyPress& _key) override;

	private:
		void resized() override;
		int getNeededColumnCount();
		int getVisibleRowCount() const;
		void updateViewportSize();
		void handleKeySelection(int _offsetX, int _offsetY, bool _shift);
		std::pair<int, int> getXY(uint32_t _index) const;

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
		float m_itemHeight = 15;
		float m_itemWidth = 140;
		uint32_t m_suggestedItemsPerRow = 32;

		GridViewport m_viewport;
		GridItemContainer m_itemContainer;

		std::map<uint32_t, std::unique_ptr<GridItem>> m_items;
		std::list<std::unique_ptr<GridItem>> m_itemPool;
		std::set<uint32_t> m_selectedItems;
		uint32_t m_lastSelectedItem = InvalidItem;
	};
}
