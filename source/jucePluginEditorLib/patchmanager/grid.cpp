#include "grid.h"

#include "griditem.h"
#include "list.h"
#include "patchmanager.h"

#include "juceUiLib/uiObject.h"

#include "../pluginEditor.h"

#include "dsp56kEmu/fastmath.h"

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

		setWantsKeyboardFocus(true);
	}

	Grid::~Grid() = default;

	void Grid::paint(juce::Graphics& g)
	{
		if(const auto c = findColor(juce::ListBox::backgroundColourId); c.getAlpha() > 0)
			g.fillAll(c);

		Component::paint(g);
	}

	void Grid::setItemWidth(const float _width)
	{
		if(m_itemWidth == _width)
			return;

		m_itemWidth = _width;
		updateViewportSize();
	}

	void Grid::setItemHeight(float _height)
	{
		if(_height <= 0.0f)
			return;

		m_itemHeight = _height;
		m_suggestedItemsPerRow = 0;
		updateViewportSize();
	}

	void Grid::setSuggestedItemsPerRow(uint32_t _count)
	{
		if(m_suggestedItemsPerRow == _count)
			return;

		m_suggestedItemsPerRow = _count;
		updateViewportSize();
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

		for (const auto& it : m_items)
		{
			const auto index = it.first;
			const auto& item = it.second;

			const auto [x,y] = getXY(index);

			item->setTopLeftPosition(static_cast<int>(m_itemWidth * static_cast<float>(x)), static_cast<int>(m_itemHeight * static_cast<float>(y)));
			item->setSize(static_cast<int>(m_itemWidth), static_cast<int>(m_itemHeight));
		}
	}

	void Grid::selectItem(const uint32_t _index, const bool _deselectOthers)
	{
		if(_deselectOthers)
		{
			if(m_selectedItems.size() == 1 && *m_selectedItems.begin() == _index)
				return;

			m_selectedItems.clear();
			m_selectedItems.insert(_index);
			m_lastSelectedItem = _index;
		}
		else if(!m_selectedItems.insert(_index).second)
			return;

		m_lastSelectedItem = _index;
		selectedRowsChanged(static_cast<int>(_index));
		repaint();
	}

	void Grid::deselectItem(const uint32_t _index)
	{
		if(!m_selectedItems.erase(_index))
			return;

		if(_index == m_lastSelectedItem)
			m_lastSelectedItem = m_selectedItems.empty() ? InvalidItem : *m_selectedItems.begin();

		repaint();
	}

	GridItem* Grid::getItem(uint32_t _index) const
	{
		const auto it = m_items.find(_index);
		return it == m_items.end() ? nullptr : it->second.get();
	}

	void Grid::selectRange(const uint32_t _newIndex)
	{
		const auto oldIndex = m_lastSelectedItem;
		const auto dir = _newIndex > oldIndex ? 1 : -1;

		for(auto i=oldIndex; ; i += dir)
		{
			selectItem(i, false);
			if(i == _newIndex)
			{
				break;
			}
		}
	}

	bool Grid::keyPressed(const juce::KeyPress& _key)
	{
		const auto shift = _key.getModifiers().isShiftDown();

		if(_key.isKeyCode(juce::KeyPress::upKey))
			handleKeySelection(0, -1, shift);
		else if(_key.isKeyCode(juce::KeyPress::downKey))
			handleKeySelection(0, 1, shift);
		else if(_key.isKeyCode(juce::KeyPress::leftKey))
			handleKeySelection(-1, 0, shift);
		else if(_key.isKeyCode(juce::KeyPress::rightKey))
			handleKeySelection(1, 0, shift);
		else
			return Component::keyPressed(_key);
		return true;
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
		const auto viewportResult = m_viewport.getVisibleRowCount();
		if(viewportResult == 0)
			return static_cast<int>(static_cast<float>(getHeight()) / m_itemHeight);
		return viewportResult;
	}

	void Grid::updateViewportSize()
	{
		const auto availableHeight = getHeight() - m_viewport.getScrollBarThickness() - 1;
		if(m_suggestedItemsPerRow)
			m_itemHeight = static_cast<float>(availableHeight) / static_cast<float>(m_suggestedItemsPerRow);

		m_itemContainer.setSize(static_cast<int>(m_itemWidth * static_cast<float>(getNeededColumnCount())), availableHeight);
		m_viewport.setSize(getWidth(), getHeight());
	}

	void Grid::handleKeySelection(const int _offsetX, const int _offsetY, bool _shift)
	{
		const auto oldXY = getXY(m_lastSelectedItem);

		const auto rows = m_viewport.getVisibleRowCount();
		const auto columns = getNeededColumnCount();

		auto [newX, newY] = oldXY;

		newX += _offsetX;
		newY += _offsetY;

		if(newY < 0)
		{
			if(newX > 0)
			{
				newY += rows;
				--newX;
			}
			else
			{
				newX = 0;
			}
		}
		if(newY >= rows)
		{
			if(newX < (columns - 1))
			{
				newY -= rows;
				++newX;
			}
		}

		if(newX >= columns)
			newX = columns - 1;
		if(newX < 0)
			newX = 0;

		const uint32_t newIndex = dsp56k::clamp(newX * rows + newY, 0, getNumRows() - 1);

		if(_shift)
		{
			selectRange(newIndex);
			ensureVisible(static_cast<int>(newIndex));
		}
		else
		{
			selectItem(newIndex, true);
			ensureVisible(static_cast<int>(newIndex));
		}
	}

	std::pair<int, int> Grid::getXY(const uint32_t _index) const
	{
		const auto c = m_viewport.getVisibleRowCount();
		const auto x = _index / c;
		const auto y = _index - x * c;
		return {static_cast<int>(x),static_cast<int>(y)};
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
		const auto pos = getEntryPosition(_row, true);
		m_viewport.autoScroll(pos.getCentreX(), pos.getCentreY(), getWidth()>>1, 1000);
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
		result.setSize(static_cast<int>(m_itemWidth), static_cast<int>(m_itemHeight));

		const auto xy = getXY(static_cast<uint32_t>(_row));

		const float x = static_cast<float>(xy.first) * m_itemWidth  - static_cast<float>(m_viewport.getViewArea().getX());
		const float y = static_cast<float>(xy.second) * m_itemHeight - static_cast<float>(m_viewport.getViewArea().getY());

		result.setPosition(static_cast<int>(x), static_cast<int>(y));

		return result;
	}
}
