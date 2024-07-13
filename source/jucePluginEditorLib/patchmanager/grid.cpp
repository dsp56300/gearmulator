#include "grid.h"

namespace jucePluginEditorLib::patchManager
{
	Grid::Grid(PatchManager& _pm) : ListModel(_pm)
	{
	}

	void Grid::mouseDown(const juce::MouseEvent& _e)
	{
		Component::mouseDown(_e);
	}

	void Grid::mouseUp(const juce::MouseEvent& _e)
	{
		Component::mouseUp(_e);
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
	}

	void Grid::redraw()
	{
	}

	void Grid::ensureVisible(int _row)
	{
		// TODO
	}

	int Grid::getSelectedEntry() const
	{
		return -1; // TODO
	}

	juce::SparseSet<int> Grid::getSelectedEntries() const
	{
		return {}; // TODO
	}

	void Grid::deselectAll()
	{
		// TODO
	}

	void Grid::setSelectedEntries(const juce::SparseSet<int>&)
	{
		// TODO
	}

	juce::Rectangle<int> Grid::getEntryPosition(int _row, bool _relativeToComponentTopLeft)
	{
		// TODO
		return {};
	}
}
