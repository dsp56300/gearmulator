#include "list.h"

#include "patchmanager.h"
#include "search.h"

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

		setContent(search);
	}

	void List::setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search)
	{
		m_search = _search;

		m_patches.clear();

		for (const auto& result : _search->results)
			m_patches.push_back(result.second);

		sortPatches();
		filterPatches();

		updateContent();
		repaint();
	}

	int List::getNumRows()
	{
		return static_cast<int>(getPatches().size());
	}

	void List::paintListBoxItem(int _rowNumber, juce::Graphics& _g, int _width, int _height, bool _rowIsSelected)
	{
		if (_rowNumber >= getNumRows())
			return;	// Juce what are you up to?

		_g.setColour(_rowIsSelected ? juce::Colours::darkblue : getLookAndFeel().findColour(textColourId));

		const auto& patch = getPatch(_rowNumber);

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
				if(i >= 0 && static_cast<size_t>(i) < getPatches().size())
					indices.add(i);
			}
		}

		return indices;
	}

	void List::selectedRowsChanged(const int lastRowSelected)
	{
		ListBoxModel::selectedRowsChanged(lastRowSelected);

		const auto idx = getSelectedRow();

		if (idx < 0 || static_cast<size_t>(idx) >= getPatches().size())
			return;

		m_patchManager.setSelectedPatch(getPatch(idx));
	}

	void List::setFilter(const std::string& _filter)
	{
		if (m_filter == _filter)
			return;

		m_filter = _filter;
		filterPatches();
		updateContent();
		repaint();
	}

	void List::sortPatches()
	{
		std::sort(m_patches.begin(), m_patches.end(), [this](const Patch& _a, const Patch& _b)
		{
			const auto sourceType = m_search->request.source.type;

			if(sourceType == pluginLib::patchDB::SourceType::Folder)
			{
				if (*_a->source != *_b->source)
					return *_a->source < *_b->source;
			}
			else if (sourceType == pluginLib::patchDB::SourceType::File || sourceType == pluginLib::patchDB::SourceType::Rom)
			{
				if (_a->program != _b->program)
					return _a->program < _b->program;
			}

			return _a->getName().compare(_b->name) < 0;
		});
	}

	void List::filterPatches()
	{
		m_filteredPatches.reserve(m_patches.size());

		m_filteredPatches.clear();

		for (const auto& patch : m_patches)
		{
			if (match(patch))
				m_filteredPatches.emplace_back(patch);
		}
	}

	bool List::match(const Patch& _patch) const
	{
		const auto name = _patch->name;
		const auto t = Search::lowercase(name);
		return t.find(m_filter) != std::string::npos;
	}
}
