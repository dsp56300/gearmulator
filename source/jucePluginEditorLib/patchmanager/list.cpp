#include "list.h"

#include "patchmanager.h"
#include "search.h"
#include "../../juceUiLib/uiObject.h"

#include "../../juceUiLib/uiObjectStyle.h"

namespace jucePluginEditorLib::patchManager
{
	List::List(PatchManager& _pm): m_patchManager(_pm)
	{
		getViewport()->setScrollBarsShown(true, false);
		setModel(this);
		setMultipleSelectionEnabled(true);

		if (const auto& t = _pm.getTemplate("pm_listbox"))
			t->apply(_pm.getEditor(), *this);
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
		const std::set<Patch> selectedPatches = getSelectedPatches();

		m_search = _search;

		m_patches.clear();

		for (const auto& result : _search->results)
			m_patches.push_back(result.second);

		sortPatches();
		filterPatches();

		updateContent();

		setSelectedPatches(selectedPatches);

		repaint();
	}

	int List::getNumRows()
	{
		return static_cast<int>(getPatches().size());
	}

	void List::paintListBoxItem(const int _rowNumber, juce::Graphics& _g, const int _width, const int _height, const bool _rowIsSelected)
	{
		const auto* style = dynamic_cast<genericUI::UiObjectStyle*>(&getLookAndFeel());

		if (_rowNumber >= getNumRows())
			return;	// Juce what are you up to?

		const auto& patch = getPatch(_rowNumber);

		const auto text = patch->getName();

		if(style && _rowIsSelected)
		{
			_g.setColour(style->getSelectedItemBackgroundColor());
			_g.fillRect(0, 0, _width, _height);
		}

		if (style)
		{
			if (const auto f = style->getFont())
				_g.setFont(*f);
		}

		_g.setColour(findColour(textColourId));
		_g.drawText(text, 2, 0, _width - 4, _height, style ? style->getAlign() : juce::Justification::centredLeft, true);
	}

	juce::var List::getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe)
	{
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

	std::set<List::Patch> List::getSelectedPatches() const
	{
		std::set<List::Patch> result;

		const auto selectedRows = getSelectedRows();
		const auto& ranges = selectedRows.getRanges();

		for (const auto& range : ranges)
		{
			for (int i = range.getStart(); i < range.getEnd(); ++i)
			{
				if (i >= 0 && static_cast<size_t>(i) < getPatches().size())
					result.insert(getPatch(i));
			}
		}
		return result;
	}

	void List::setSelectedPatches(const std::set<Patch>& _patches)
	{
		if (_patches.empty())
			return;

		juce::SparseSet<int> selection;

		int maxRow = std::numeric_limits<int>::min();
		int minRow = std::numeric_limits<int>::max();

		for(int i=0; i<static_cast<int>(getPatches().size()); ++i)
		{
			if (_patches.find(getPatch(i)) != _patches.end())
			{
				selection.addRange({ i, i + 1 });

				maxRow = std::max(maxRow, i);
				minRow = std::min(minRow, i);
			}
		}

		setSelectedRows(selection);
		scrollToEnsureRowIsOnscreen((minRow + maxRow) >> 1);
	}

	void List::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		if (!m_search)
			return;

		if (_dirty.searches.empty())
			return;

		if(_dirty.searches.find(m_search->handle) != _dirty.searches.end())
			setContent(m_search);
	}

	void List::setFilter(const std::string& _filter)
	{
		if (m_filter == _filter)
			return;

		const auto selected = getSelectedPatches();

		m_filter = _filter;

		filterPatches();
		updateContent();

		setSelectedPatches(selected);

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
