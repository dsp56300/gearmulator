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

		const auto text = patch->getName();

		_g.drawText(text, 2, 0, _width - 4, _height, juce::Justification::centredLeft, true);
//		_g.setColour(m_patchList.getLookAndFeel().findColour(ListBox::backgroundColourId));
//		_g.fillRect(_width - 1, 0, 1, _height);
	}

	juce::var List::getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe)
	{
		if(rowsToDescribe.size() > 1)
			return {};

		const auto& ranges = rowsToDescribe.getRanges();

		juce::Array<juce::var> indices;

		for (const auto& range : ranges)
		{
			for (int i = range.getStart(); i < range.getEnd(); ++i)
			{
				if(i >= 0 && i<m_patches.size())
					indices.add(i);
			}
		}

		return indices;
	}

	void List::selectedRowsChanged(int lastRowSelected)
	{
		ListBoxModel::selectedRowsChanged(lastRowSelected);

		const auto idx = getSelectedRow();

		if (idx < 0 || idx >= m_patches.size())
			return;

		m_patchManager.setSelectedPatch(m_patches[idx]);
	}
}
