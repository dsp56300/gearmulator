#include "gridviewport.h"

#include "grid.h"

namespace jucePluginEditorLib::patchManager
{
	GridViewport::GridViewport(Grid& _grid) : m_grid(_grid)
	{
		setWantsKeyboardFocus(false);
	}

	void GridViewport::visibleAreaChanged(const juce::Rectangle<int>& _area)
	{
		Viewport::visibleAreaChanged(_area);

		m_grid.setVisibleItemRange(getItemRangeFromArea(_area));
	}

	std::pair<int, int> GridViewport::getItemRangeFromArea(const juce::Rectangle<int>& _area) const
	{
		const auto xOffset = _area.getX();

		const auto firstColumn = xOffset / m_grid.getItemWidth();
		const auto lastColumn = (xOffset + _area.getWidth() + m_grid.getItemWidth() - 1) / m_grid.getItemWidth();

		const auto rowCount = _area.getHeight() / m_grid.getItemHeight();

		const auto firstItem = firstColumn * rowCount;
		const auto itemCount = (lastColumn - firstColumn) * rowCount + rowCount;

		return {firstItem, itemCount};
	}

	int GridViewport::getVisibleRowCount() const
	{
		return getViewArea().getHeight() / m_grid.getItemHeight();
	}
}
