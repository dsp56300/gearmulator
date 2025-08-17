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
		genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Warning, "Confirmation needed", "Delete selected patches from bank?", std::move(_callback));
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
