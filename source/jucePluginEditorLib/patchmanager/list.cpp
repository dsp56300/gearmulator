#include "list.h"

#include "listitem.h"
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

	void List::refreshContent()
	{
		setContent(m_search);
	}

	void List::setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search)
	{
		const std::set<Patch> selectedPatches = getSelectedPatches();

		m_search = _search;

		m_patches.clear();
		{
			std::shared_lock lock(_search->resultsMutex);
			m_patches.insert(m_patches.end(), _search->results.begin(), _search->results.end());
		}

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

		if (ranges.isEmpty())
			return {};

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

	juce::Component* List::refreshComponentForRow(int rowNumber, bool isRowSelected,
		Component* existingComponentToUpdate)
	{
		auto* existing = dynamic_cast<ListItem*>(existingComponentToUpdate);

		if (existing)
		{
			existing->setRow(rowNumber);
			return existing;
		}

		delete existingComponentToUpdate;

		return new ListItem(*this, rowNumber);
	}

	void List::selectedRowsChanged(const int lastRowSelected)
	{
		ListBoxModel::selectedRowsChanged(lastRowSelected);

		const auto patches = getSelectedPatches();

		if(patches.size() == 1)
			m_patchManager.setSelectedPatch(*patches.begin(), m_search->handle, lastRowSelected);
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

	std::vector<pluginLib::patchDB::PatchPtr> List::getPatchesFromDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) const
	{
		const auto* arr = _dragSourceDetails.description.getArray();
		if (!arr)
			return {};

		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (const auto& var : *arr)
		{
			if (!var.isInt())
				continue;
			const int idx = var;
			if (const auto patch = getPatch(idx))
				patches.push_back(patch);
		}

		return patches;
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

	void List::sortPatches(Patches& _patches, pluginLib::patchDB::SourceType _sourceType)
	{
		std::sort(_patches.begin(), _patches.end(), [_sourceType](const Patch& _a, const Patch& _b)
		{
			const auto sourceType = _sourceType;

			if(sourceType == pluginLib::patchDB::SourceType::Folder)
			{
				const auto aSource = _a->source.lock();
				const auto bSource = _b->source.lock();
				if (*aSource != *bSource)
					return *aSource < *bSource;
			}
			else if (sourceType == pluginLib::patchDB::SourceType::File || sourceType == pluginLib::patchDB::SourceType::Rom || sourceType == pluginLib::patchDB::SourceType::LocalStorage)
			{
				if (_a->program != _b->program)
					return _a->program < _b->program;
			}

			return _a->getName().compare(_b->name) < 0;
		});
	}

	void List::sortPatches()
	{
		// Note: If this list is no longer sorted by calling this function, be sure to modify the second caller in state.cpp, too, as it is used to track the selected entry across multiple parts
		sortPatches(m_patches, m_search->getSourceType());
	}

	void List::filterPatches()
	{
		if (m_filter.empty())
		{
			m_filteredPatches.clear();
			return;
		}

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
