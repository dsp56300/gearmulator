#include "grid.h"

#include "griditem.h"
#include "list.h"
#include "patchmanager.h"

#include "../../juceUiLib/uiObjectStyle.h"
#include "../../juceUiLib/uiObject.h"
#include "../pluginEditor.h"

namespace jucePluginEditorLib::patchManager
{
	Grid::Grid(PatchManager& _pm) : ListModel(_pm), m_viewport(*this), m_itemContainer(*this)
	{
		m_viewport.setScrollBarsShown(false, true);
		m_viewport.setViewedComponent(&m_itemContainer, false);

		addAndMakeVisible(m_viewport);

		if (const auto& t = _pm.getTemplate("pm_listbox"))
			t->apply(_pm.getEditor(), *this);

		List::applyStyleToViewport(_pm, m_viewport);
	}

	void Grid::paint(juce::Graphics& g)
	{
		if(const auto c = findColor(juce::ListBox::backgroundColourId); c.getAlpha() > 0)
			g.fillAll(c);

		Component::paint(g);
	}

	void Grid::setVisibleItemRange(const std::pair<uint32_t, uint32_t>& _range)
	{
		const auto& start = _range.first;
		const auto& end = start + _range.second;

		// move items not in range back to pool first
		for(auto it = m_items.begin(); it != m_items.end();)
		{
			const auto index = it->first;

			if(index < start || index >= end)
			{
				m_itemPool.emplace_back(std::move(it->second));
				it = m_items.erase(it);
			}
			else
			{
				++it;
			}
		}

		// now allocate new items or get them from pool for any item that we do not have yet
		for(auto i=start; i<end; ++i)
		{
			auto it = m_items.find(i);

			if(it != m_items.end())
				continue;

			std::unique_ptr<GridItem> item;

			if(!m_itemPool.empty())
			{
				item = std::move(m_itemPool.back());
				m_itemPool.pop_back();
			}

			if(!item)
				item.reset(new GridItem(*this));

			item->setItem(i, refreshComponentForRow(static_cast<int>(i), false, item->getItem()));

			if(item->getParentComponent() != &m_itemContainer)
				m_itemContainer.addAndMakeVisible(item.get());

			m_items.insert({i, std::move(item)});
		}

		const auto visibleRowCount = m_viewport.getVisibleRowCount();

		for (const auto& it : m_items)
		{
			const auto index = it.first;
			const auto& item = it.second;

			const auto x = index / visibleRowCount;
			const auto y = index - x * visibleRowCount;

			item->setTopLeftPosition(static_cast<int>(m_itemWidth * x), static_cast<int>(m_itemHeight * y));
			item->setSize(m_itemWidth, m_itemHeight);
		}
	}

	void Grid::selectItem(uint32_t _index, bool _deselectOthers)
	{
		if(_deselectOthers)
		{
			if(m_selectedItems.size() == 1 && *m_selectedItems.begin() == _index)
				return;

			m_selectedItems.clear();
			m_selectedItems.insert(_index);
		}
		else if(!m_selectedItems.insert(_index).second)
			return;

		selectedRowsChanged(static_cast<int>(_index));
		repaint();
	}

	void Grid::deselectItem(const uint32_t _index)
	{
		if(!m_selectedItems.erase(_index))
			return;

		repaint();
	}

	GridItem* Grid::getItem(uint32_t _index) const
	{
		const auto it = m_items.find(_index);
		return it == m_items.end() ? nullptr : it->second.get();
	}

	void Grid::resized()
	{
		updateViewportSize();
	}

	int Grid::getNeededColumnCount()
	{
		const auto numItems = getNumRows();
		const auto itemsPerColumn = getVisibleRowCount();
		const auto columnCount = (numItems + itemsPerColumn - 1) / itemsPerColumn;
		return columnCount;
	}

	int Grid::getVisibleRowCount() const
	{
		return getHeight() / m_itemHeight;
	}

	void Grid::updateViewportSize()
	{
		m_itemContainer.setSize(m_itemWidth * getNeededColumnCount(), getHeight());
		m_viewport.setSize(getWidth(), getHeight());
	}

	juce::Colour Grid::findColor(const int _colorId)
	{
		return findColour(_colorId);
	}

	const juce::LookAndFeel& Grid::getStyle() const
	{
		return getLookAndFeel();
	}

	void Grid::onModelChanged()
	{
		updateViewportSize();
		repaint();
	}

	void Grid::redraw()
	{
		repaint();
	}

	void Grid::ensureVisible(int _row)
	{
		// TODO
	}

	int Grid::getSelectedEntry() const
	{
		if (m_selectedItems.empty())
			return -1;

		return static_cast<int>(*m_selectedItems.begin());
	}

	juce::SparseSet<int> Grid::getSelectedEntries() const
	{
		return toSparseSet(m_selectedItems);
	}

	void Grid::deselectAll()
	{
		if(m_selectedItems.empty())
			return;

		m_selectedItems.clear();
		repaint();
	}

	void Grid::setSelectedEntries(const juce::SparseSet<int>& _items)
	{
		m_selectedItems = toSet<uint32_t>(_items);
		repaint();
	}

	juce::Rectangle<int> Grid::getEntryPosition(const int _row, bool _relativeToComponentTopLeft)
	{
		juce::Rectangle<int> result;
		result.setSize(m_itemWidth, m_itemHeight);

		const auto x = _row / m_viewport.getVisibleRowCount();
		const auto y = _row - x * m_viewport.getVisibleRowCount();

		result.setPosition(x * m_itemWidth - m_viewport.getViewArea().getX(), y * m_itemHeight - m_viewport.getViewArea().getY());

		return result;
	}
}
