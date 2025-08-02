#include "listmodel.h"

#include "defaultskin.h"
#include "listitem.h"
#include "patchmanager.h"
#include "patchmanageruijuce.h"
#include "savepatchdesc.h"
#include "search.h"
#include "treeitem.h"

#include "../pluginEditor.h"

#include "juceUiLib/uiObjectStyle.h"

#include "jucePluginLib/filetype.h"
#include "jucePluginLib/patchdb/db.h"

namespace jucePluginEditorLib::patchManager
{
	ListModel::ListModel(PatchManagerUiJuce& _pm): m_patchManager(_pm)
	{
	}

	void ListModel::setContent(const pluginLib::patchDB::SearchHandle& _handle)
	{
		cancelSearch();

		const auto& search = m_patchManager.getDB().getSearch(_handle);

		if (!search)
			return;

		setContent(search);
	}

	void ListModel::setContent(pluginLib::patchDB::SearchRequest&& _request)
	{
		cancelSearch();
		const auto sh = getDB().search(std::move(_request));
		setContent(sh);
		m_searchHandle = sh;
	}

	void ListModel::clear()
	{
		m_search.reset();
		m_patches.clear();
		m_filteredPatches.clear();
		onModelChanged();
		getPatchManager().setListStatus(0, 0);
	}

	void ListModel::refreshContent()
	{
		setContent(m_search);
	}

	void ListModel::setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search)
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

		onModelChanged();

		setSelectedPatches(selectedPatches);

		redraw();

		getPatchManager().setListStatus(static_cast<uint32_t>(selectedPatches.size()), static_cast<uint32_t>(getPatches().size()));
	}

	bool ListModel::exportPresets(const bool _selectedOnly, const pluginLib::FileType& _fileType) const
	{
		Patches patches;

		if(_selectedOnly)
		{
			const auto selected = getSelectedPatches();
			if(selected.empty())
				return false;
			patches.assign(selected.begin(), selected.end());
		}
		else
		{
			patches = getPatches();
		}

		if(patches.empty())
			return false;

		return getDB().exportPresets(std::move(patches), _fileType);
	}

	bool ListModel::onClicked(const juce::MouseEvent& _mouseEvent)
	{
		return true;
	}

	void ListModel::cancelSearch()
	{
		if(m_searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return;
		getDB().cancelSearch(m_searchHandle);
		m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	}

	int ListModel::getNumRows()
	{
		return static_cast<int>(getPatches().size());
	}

	void ListModel::paintListBoxItem(const int _rowNumber, juce::Graphics& _g, const int _width, const int _height, const bool _rowIsSelected)
	{
		if (_rowNumber >= getNumRows())
			return;	// Juce what are you up to?

		const auto* style = dynamic_cast<const genericUI::UiObjectStyle*>(&getStyle());

		const auto& patch = getPatch(_rowNumber);

		const auto text = patch->getName();

		if(_rowIsSelected)
		{
			if(style)
				_g.setColour(style->getSelectedItemBackgroundColor());
			else
				_g.setColour(juce::Colour(0x33ffffff));
			_g.fillRect(0, 0, _width, _height);
		}

		if (style)
		{
			_g.setImageResamplingQuality(style->getAntialiasing() ? juce::Graphics::highResamplingQuality : juce::Graphics::lowResamplingQuality);
			if (const auto f = style->getFont())
				_g.setFont(*f);
		}

		const auto c = getPatchManager().getPatchColor(patch);

		constexpr int offsetX = 20;

		if(c != pluginLib::patchDB::g_invalidColor)
		{
			_g.setColour(juce::Colour(c));
			constexpr auto s = 8.f;
			constexpr auto sd2 = 0.5f * s;
			_g.fillEllipse(10 - sd2, static_cast<float>(_height) * 0.5f - sd2, s, s);
//			_g.setColour(juce::Colour(0xffffffff));
//			_g.drawEllipse(10 - sd2, static_cast<float>(_height) * 0.5f - sd2, s, s, 1.0f);
//			offsetX += 14;
		}

//		if(c != pluginLib::patchDB::g_invalidColor)
//			_g.setColour(juce::Colour(c));
//		else
		_g.setColour(findColor(juce::ListBox::textColourId));

		_g.drawText(text, offsetX, 0, _width - 4, _height, style ? style->getAlign() : juce::Justification::centredLeft, true);
	}

	juce::var ListModel::getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe)
	{
		const auto& ranges = rowsToDescribe.getRanges();

		if (ranges.isEmpty())
			return {};

		std::map<uint32_t, pluginLib::patchDB::PatchPtr> patches;

		for (const auto& range : ranges)
		{
			for (int i = range.getStart(); i < range.getEnd(); ++i)
			{
				if(i >= 0 && static_cast<size_t>(i) < getPatches().size())
					patches.insert({i, getPatches()[i]});
			}
		}

		return {};//new SavePatchDesc(getDB(), std::move(patches));
	}

	juce::Component* ListModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate)
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

	void ListModel::selectedRowsChanged(const int lastRowSelected)
	{
		ListBoxModel::selectedRowsChanged(lastRowSelected);

		if(!m_ignoreSelectedRowsChanged)
			activateSelectedPatch();

		const auto patches = getSelectedPatches();
		getPatchManager().setListStatus(static_cast<uint32_t>(patches.size()), static_cast<uint32_t>(getPatches().size()));
	}

	std::set<ListModel::Patch> ListModel::getSelectedPatches() const
	{
		std::set<Patch> result;

		const auto selectedRows = getSelectedEntries();
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

	bool ListModel::setSelectedPatches(const std::set<Patch>& _patches)
	{
		if (_patches.empty())
			return false;

		std::set<pluginLib::patchDB::PatchKey> patches;

		for (const auto& patch : _patches)
		{
			if(!patch->source.expired())
				patches.insert(pluginLib::patchDB::PatchKey(*patch));
		}
		return setSelectedPatches(patches);
	}

	bool ListModel::setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches)
	{
		if (_patches.empty())
		{
			deselectAll();
			return false;
		}

		juce::SparseSet<int> selection;

		int maxRow = std::numeric_limits<int>::min();
		int minRow = std::numeric_limits<int>::max();

		for(int i=0; i<static_cast<int>(getPatches().size()); ++i)
		{
			const auto key = pluginLib::patchDB::PatchKey(*getPatch(i));

			if (_patches.find(key) != _patches.end())
			{
				selection.addRange({ i, i + 1 });

				maxRow = std::max(maxRow, i);
				minRow = std::min(minRow, i);
			}
		}

		if(selection.isEmpty())
		{
			deselectAll();
			return false;
		}

		m_ignoreSelectedRowsChanged = true;
		setSelectedEntries(selection);
		m_ignoreSelectedRowsChanged = false;
		ensureVisible((minRow + maxRow) >> 1);
		return true;
	}

	void ListModel::activateSelectedPatch() const
	{
		const auto patches = getSelectedPatches();

		if(patches.size() == 1)
			getDB().setSelectedPatch(*patches.begin(), m_search->handle);
	}

	void ListModel::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		if (!m_search)
			return;

		if (_dirty.searches.empty())
			return;

		if(_dirty.searches.find(m_search->handle) != _dirty.searches.end())
			setContent(m_search);
	}

	pluginLib::patchDB::DataSourceNodePtr ListModel::getDataSource() const
	{
		if(!m_search)
			return nullptr;

		return m_search->request.sourceNode;
	}

	PatchManager& ListModel::getDB() const
	{
		return m_patchManager.getDB();
	}

	void ListModel::listBoxItemClicked(const int _row, const juce::MouseEvent& _mouseEvent)
	{
		if(!onClicked(_mouseEvent))
			ListBoxModel::listBoxItemClicked(_row, _mouseEvent);
	}

	void ListModel::backgroundClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!onClicked(_mouseEvent))
			ListBoxModel::backgroundClicked(_mouseEvent);
	}

	void ListModel::showDeleteConfirmationMessageBox(genericUI::MessageBox::Callback _callback)
	{
		genericUI::MessageBox::showYesNo(juce::MessageBoxIconType::WarningIcon, "Confirmation needed", "Delete selected patches from bank?", std::move(_callback));
	}

	pluginLib::patchDB::SourceType ListModel::getSourceType() const
	{
		if(!m_search)
			return pluginLib::patchDB::SourceType::Invalid;
		return m_search->getSourceType();
	}

	bool ListModel::canReorderPatches() const
	{
		if(!m_search)
			return false;
		if(getSourceType() != pluginLib::patchDB::SourceType::LocalStorage)
			return false;
		if(!m_search->request.tags.empty())
			return false;
		return true;
	}

	bool ListModel::hasTagFilters() const
	{
		if(!m_search)
			return false;
		return !m_search->request.tags.empty();
	}

	bool ListModel::hasFilters() const
	{
		return hasTagFilters();
	}

	pluginLib::patchDB::SearchHandle ListModel::getSearchHandle() const
	{
		if(!m_search)
			return pluginLib::patchDB::g_invalidSearchHandle;
		return m_search->handle;
	}

	void ListModel::sortPatches()
	{
		PatchManagerUi::sortPatches(m_patches, getSourceType());
	}

	void ListModel::filterPatches()
	{
		if (!m_hideDuplicatesByHash && !m_hideDuplicatesByName)
		{
			m_filteredPatches.clear();
			return;
		}

		m_filteredPatches.reserve(m_patches.size());
		m_filteredPatches.clear();

		std::set<pluginLib::patchDB::PatchHash> knownHashes;
		std::set<std::string> knownNames;

		for (const auto& patch : m_patches)
		{
			if(m_hideDuplicatesByHash)
			{
				if(knownHashes.find(patch->hash) != knownHashes.end())
					continue;
				knownHashes.insert(patch->hash);
			}

			if(m_hideDuplicatesByName)
			{
				if(knownNames.find(patch->getName()) != knownNames.end())
					continue;
				knownNames.insert(patch->getName());
			}

			if (true || match(patch))
				m_filteredPatches.emplace_back(patch);
		}
	}

	bool ListModel::match(const Patch& _patch) const
	{
		return true;
	}

	bool ListModel::isInterestedInDragSource(const SourceDetails& dragSourceDetails)
	{
		auto ds = getPatchManager().getSelectedDataSourceTreeItem();
		if (!ds)
			return false;
		return ds->isInterestedInDragSource(dragSourceDetails);
	}

	void ListModel::itemDropped(const SourceDetails& dragSourceDetails)
	{
		auto ds = getPatchManager().getSelectedDataSourceTreeItem();
		if (!ds)
			return;
		return ds->itemDropped(dragSourceDetails, std::numeric_limits<int>::max());
	}

	bool ListModel::isInterestedInFileDrag(const juce::StringArray& files)
	{
		auto ds = getPatchManager().getSelectedDataSourceTreeItem();
		if (!ds)
			return false;
		return ds->isInterestedInFileDrag(files);
	}

	void ListModel::filesDropped(const juce::StringArray& files, int x, int y)
	{
		auto ds = getPatchManager().getSelectedDataSourceTreeItem();
		if (!ds)
			return;
		ds->filesDropped(files, std::numeric_limits<int>::max());
	}
}
