#include "list.h"

#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	List::List(PatchManager& _pm): m_patchManager(_pm)
	{
		getViewport()->setScrollBarsShown(true, false);
		setModel(this);
	}

	void List::setContent(const pluginLib::patchDB::SearchHandle& _handle)
	{
		const auto& search = m_patchManager.getSearch(_handle);

		if (!search)
			return;

		setContent(*search);
	}

	void List::setContent(const pluginLib::patchDB::Search& _search)
	{
		m_patches.clear();

		for (const auto& result : _search.results)
			m_patches.push_back(result.second);

		updateContent();
		repaint();
	}

	int List::getNumRows()
	{
		return static_cast<int>(m_patches.size());
	}

	void List::paintListBoxItem(int _rowNumber, juce::Graphics& _g, int _width, int _height, bool _rowIsSelected)
	{
		if (_rowNumber >= getNumRows())
			return;	// Juce what are you up to?

		_g.setColour(_rowIsSelected ? juce::Colours::darkblue : getLookAndFeel().findColour(textColourId));

		const auto& patch = m_patches[_rowNumber];

		const auto text = patch->name;

		_g.drawText(text, 2, 0, _width - 4, _height, juce::Justification::centredLeft, true);
//		_g.setColour(m_patchList.getLookAndFeel().findColour(ListBox::backgroundColourId));
//		_g.fillRect(_width - 1, 0, 1, _height);
	}

	juce::var List::getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe)
	{
		if(rowsToDescribe.size() > 1)
			return {};

		const auto row = rowsToDescribe.getRange(0).getStart();
		if (row < 0 || row >= m_patches.size())
			return {};
		return row;
	}
}
