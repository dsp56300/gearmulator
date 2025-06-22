#pragma once

#include <vector>

#include "jucePluginLib/patchdb/patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct SearchRequest;
	struct Search;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerUiRml;
}

namespace juceRmlUi
{
	class ElemList;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class ListModel
	{
	public:
		using Patch = pluginLib::patchDB::PatchPtr;
		using Patches = std::vector<Patch>;

		explicit ListModel(PatchManagerUiRml& _pm, juceRmlUi::ElemList* _list);

		PatchManagerUiRml& getPatchManager() const;
		patchManager::PatchManager& getDB() const;

		pluginLib::patchDB::SearchHandle getSearchHandle() const;
		std::set<Patch> getSelectedPatches();
		void setSelectedPatches(const std::set<Patch>& _patches);

		std::vector<size_t> getSelectedEntries() const;
		void setSelectedEntries(const std::vector<size_t>& _indices);

		void setContent(pluginLib::patchDB::SearchHandle _searchHandle);
		void setContent(pluginLib::patchDB::SearchRequest&& _search);
		void setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search);

		void cancelSearch();

		void setVisible(bool _visible);

		const Patches& getPatches() const
		{
			if (m_filter.empty() && !m_hideDuplicatesByHash && !m_hideDuplicatesByName)
				return m_patches;
			return m_filteredPatches;
		}

		void filterPatches();
		void sortPatches();
		bool match(const Patch& _patch) const;

		pluginLib::patchDB::SourceType getSourceType() const;

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

	private:
		void updateEntries();

		PatchManagerUiRml& m_patchManager;
		juceRmlUi::ElemList* m_list;

		std::shared_ptr<pluginLib::patchDB::Search> m_search;
		Patches m_patches;
		Patches m_filteredPatches;
		std::string m_filter;
		bool m_hideDuplicatesByHash = false;
		bool m_hideDuplicatesByName = false;
		pluginLib::patchDB::SearchHandle m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	};
}
