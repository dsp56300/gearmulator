#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class Grid;

	class GridViewport : public juce::Viewport
	{
	public:
		GridViewport(Grid& _grid);

		void visibleAreaChanged(const juce::Rectangle<int>& _area) override;

		std::pair<int, int> getItemRangeFromArea(const juce::Rectangle<int>& _area) const;

		int getVisibleRowCount() const;

	private:
		Grid& m_grid;
	};
}