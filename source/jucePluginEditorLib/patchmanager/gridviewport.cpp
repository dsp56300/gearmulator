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
		const auto xOffset = static_cast<float>(_area.getX());

		const auto firstColumn = static_cast<int>(xOffset / m_grid.getItemWidth());
		const auto lastColumn = static_cast<int>((xOffset + static_cast<float>(_area.getWidth()) + m_grid.getItemWidth() - 1.0f) / m_grid.getItemWidth());

		const auto rowCount = static_cast<int>(static_cast<float>(_area.getHeight()) / m_grid.getItemHeight());

		const auto firstItem = firstColumn * rowCount;
		const auto itemCount = (lastColumn - firstColumn) * rowCount + rowCount;

		return {firstItem, itemCount};
	}

	int GridViewport::getVisibleRowCount() const
	{
		return static_cast<int>(static_cast<float>(getViewArea().getHeight()) / m_grid.getItemHeight());
	}
}
