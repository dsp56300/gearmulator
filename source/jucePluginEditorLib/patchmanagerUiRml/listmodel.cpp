#include "listmodel.h"

#include "listitem.h"
#include "patchmanagerUiRml.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"
#include "jucePluginEditorLib/patchmanager/search.h"

#include "juceRmlUi/rmlElemList.h"

namespace jucePluginEditorLib::patchManagerRml
{
	namespace
	{
		Rml::ElementInstancerGeneric<ListElemEntry> g_instancer;
	}

	ListModel::ListModel(PatchManagerUiRml& _pm, juceRmlUi::ElemList* _list) : m_patchManager(_pm), m_list(_list)
	{
		_list->setInstancer(&g_instancer);

		auto& list = _list->getList();

		list.setMultiselect(true);
		list.evSelectionChanged.addListener([this](juceRmlUi::List* const&) { onSelectionChanged(); });
	}

	PatchManagerUiRml& ListModel::getPatchManager() const
	{
		return m_patchManager;
	}

	patchManager::PatchManager& ListModel::getDB() const
	{
		return m_patchManager.getDB();
	}

	pluginLib::patchDB::SearchHandle ListModel::getSearchHandle() const
	{
		if (!m_search)
			return pluginLib::patchDB::g_invalidSearchHandle;
		return m_search->handle;
	}

	std::set<ListModel::Patch> ListModel::getSelectedPatches() const
	{
		auto indices = getSelectedEntries();

		if (indices.empty())
			return {};

		std::set<Patch> result;

		const auto& patches = getPatches();

		for (size_t index : indices)
			result.insert(patches[index]);

		return result;
	}

	void ListModel::setSelectedPatches(const std::set<Patch>& _selection)
	{
		if (_selection.empty())
		{
			m_list->getList().deselectAll();
			return;
		}

		const auto& patches = getPatches();

		std::vector<size_t> indices;

		for (size_t i=0; i<patches.size(); ++i)
		{
			if (_selection.find(patches[i]) != _selection.end())
				indices.push_back(i);
		}

		setSelectedEntries(indices);
	}

	std::vector<size_t> ListModel::getSelectedEntries() const
	{
		return m_list->getList().getSelectedIndices();
	}

	void ListModel::setSelectedEntries(const std::vector<size_t>& _indices)
	{
		m_activateOnSelectionChange = false;
		m_list->getList().setSelectedIndices(_indices);
		m_activateOnSelectionChange = true;
	}

	void ListModel::setContent(pluginLib::patchDB::SearchHandle _searchHandle)
	{
		cancelSearch();

		const auto& search = m_patchManager.getDB().getSearch(_searchHandle);

		if (!search)
			return;

		setContent(search);
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

		updateEntries();

		setSelectedPatches(selectedPatches);

		getPatchManager().setListStatus(static_cast<uint32_t>(selectedPatches.size()), static_cast<uint32_t>(getPatches().size()));
	}

	void ListModel::setContent(pluginLib::patchDB::SearchRequest&& _search)
	{
		cancelSearch();
		const auto sh = getDB().search(std::move(_search));
		setContent(sh);
		m_searchHandle = sh;
	}

	void ListModel::cancelSearch()
	{
		if(m_searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return;
		getDB().cancelSearch(m_searchHandle);
		m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	}

	void ListModel::filterPatches()
	{
		if (m_filter.empty() && !m_hideDuplicatesByHash && !m_hideDuplicatesByName)
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

			if (m_filter.empty() || match(patch))
				m_filteredPatches.emplace_back(patch);
		}
	}

	void ListModel::sortPatches()
	{
		patchManager::PatchManagerUi::sortPatches(m_patches, getSourceType());
	}

	bool ListModel::match(const Patch& _patch) const
	{
		const auto name = _patch->getName();
		const auto t = patchManager::Search::lowercase(name);
		return t.find(m_filter) != std::string::npos;
	}

	pluginLib::patchDB::SourceType ListModel::getSourceType() const
	{
		if(!m_search)
			return pluginLib::patchDB::SourceType::Invalid;
		return m_search->getSourceType();
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

	void ListModel::activateSelectedPatch() const
	{
		const auto patches = getSelectedPatches();

		if(patches.size() == 1)
			getDB().setSelectedPatch(*patches.begin(), m_search->handle);
	}

	void ListModel::updateEntries() const
	{
		const auto& patches = getPatches();

		auto& list = m_list->getList();

		// remove entries if we have too many
		while (list.size() > patches.size())
			list.removeEntry(list.size() - 1);

		// either update existing entries or create new ones
		for (size_t i = 0; i < patches.size(); ++i)
		{
			if (i < list.size())
			{
				auto* entry = dynamic_cast<ListItem*>(list.getEntry(i).get());
				assert(entry);
				if (entry)
					entry->setPatch(patches[i]);
			}
			else
			{
				auto entry = std::make_shared<ListItem>(m_patchManager, list);
				entry->setPatch(patches[i]);
				list.addEntry(std::move(entry));
			}
		}
	}

	void ListModel::onSelectionChanged() const
	{
		if (m_activateOnSelectionChange)
			activateSelectedPatch();

		const auto patches = getSelectedPatches();

		getPatchManager().setListStatus(static_cast<uint32_t>(patches.size()), static_cast<uint32_t>(getPatches().size()));
	}
}
