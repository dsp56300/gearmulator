#include "griditemcontainer.h"

#include "grid.h"
#include "griditem.h"

namespace jucePluginEditorLib::patchManager
{
	GridItemContainer::GridItemContainer(Grid& _grid) : m_grid(_grid)
	{
	}

	void GridItemContainer::mouseDown(const juce::MouseEvent& _e)
	{
		Component::mouseDown(_e);
		m_isDragging = false;
		m_itemIndexMouseDown = mouseToItemIndex(_e);
	}

	juce::ScaledImage GridItemContainer::createSnapshotOfRows(const juce::SparseSet<int>& _rows, int& _x, int& _y) const
	{
		juce::Rectangle<int> imageArea;

		const auto rows = Grid::toSet<uint32_t>(_rows);
		for (const auto row : rows)
		{
            if (const auto* rowComp = m_grid.getItem(row))
            {
                auto pos = m_grid.getLocalPoint(rowComp, juce::Point<int>());

                imageArea = imageArea.getUnion ({ pos.x, pos.y, rowComp->getWidth(), rowComp->getHeight() });
            }
		}

		if(imageArea.isEmpty())
			return {};

	    imageArea = imageArea.getIntersection (getLocalBounds());
	    _x = imageArea.getX();
	    _y = imageArea.getY();

		constexpr auto additionalScale = 2.0f;
	    const auto listScale = Component::getApproximateScaleFactorForComponent (this) * additionalScale;
		juce::Image snapshot (juce::Image::ARGB,
		                      juce::roundToInt (static_cast<float>(imageArea.getWidth()) * listScale),
		                      juce::roundToInt (static_cast<float>(imageArea.getHeight()) * listScale),
		                      true);

		for (const auto row : rows)
		{
            if (auto* rowComp = m_grid.getItem(row))
            {
	            juce::Graphics g (snapshot);
                g.setOrigin ((getLocalPoint (rowComp, juce::Point<int>()) - imageArea.getPosition()) * additionalScale);

                const auto rowScale = getApproximateScaleFactorForComponent(rowComp) * additionalScale;

                if (g.reduceClipRegion (rowComp->getLocalBounds() * rowScale))
                {
                    g.beginTransparencyLayer (0.6f);
                    g.addTransform (juce::AffineTransform::scale (rowScale));
                    rowComp->paintEntireComponent (g, false);
                    g.endTransparencyLayer();
                }
            }
	    }

	    return { snapshot, additionalScale };
	}

	void GridItemContainer::mouseDrag(const juce::MouseEvent& _e)
	{
		m_itemIndexMouseDown = InvalidItem;

		if(m_isDragging)
			return;

		juce::SparseSet<int> rows;
		const auto hoveredIndex = mouseToItemIndex(_e);
		if(m_grid.isSelected(hoveredIndex))
			rows = m_grid.getSelectedEntries();
		else
			rows.addRange(juce::Range<int>(static_cast<int>(hoveredIndex), static_cast<int>(hoveredIndex)+1));

		if(!rows.isEmpty())
		{
			const auto dragDescription = m_grid.getDragSourceDescription(rows);

			if(!dragDescription.isVoid())
			{
			    if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor (this))
			    {
			        int x, y;
			        const auto dragImage = createSnapshotOfRows(rows, x, y);
					if(dragImage.getImage().isValid())
					{
				        const auto p = juce::Point<int> (x, y) - _e.getEventRelativeTo (this).position.toInt();
				        dragContainer->startDragging (dragDescription, &m_grid, dragImage, true, &p, &_e.source);
						m_isDragging = true;
					}
			    }
			}
		}

		Component::mouseDrag(_e);
	}

	void GridItemContainer::mouseUp(const juce::MouseEvent& _e)
	{
		const auto i = mouseToItemIndex(_e);

		if(i == m_itemIndexMouseDown)
		{
			const auto cmd = _e.mods.isCommandDown();
			const auto shift = _e.mods.isShiftDown();

			if(m_grid.isSelected(i) && cmd)
			{
				m_grid.deselectItem(i);
			}
			else if(shift)
			{
				m_grid.selectRange(i);
			}
			else
			{
				m_grid.selectItem(i, !cmd);
				m_grid.listBoxItemClicked(static_cast<int>(i), _e);
			}
		}
	}

	uint32_t GridItemContainer::mouseToItemIndex(const juce::MouseEvent& _e) const
	{
		const auto rowsPerCol = m_grid.getViewport().getVisibleRowCount();

		const auto col = static_cast<float>(_e.x) / m_grid.getItemWidth();
		const auto row = static_cast<float>(_e.y) / m_grid.getItemHeight();

		return static_cast<int>(row) + static_cast<int>(col) * rowsPerCol;
	}
}
