#pragma once

#include <vector>

#include "jucePluginLib/patchdb/patchdbtypes.h"

#include "juceUiLib/messageBox.h"

namespace Rml
{
	class Event;
}

namespace pluginLib
{
	class FileType;
}

namespace pluginLib::patchDB
{
	struct PatchKey;
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
		std::set<Patch> getSelectedPatches() const;
		bool setSelectedPatches(const std::set<Patch>& _selection);
		bool setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _selection);

		std::vector<size_t> getSelectedEntries() const;
		void setSelectedEntries(const std::vector<size_t>& _indices);

		void setContent(pluginLib::patchDB::SearchHandle _searchHandle);
		void setContent(pluginLib::patchDB::SearchRequest&& _search);
		void setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search);

		void cancelSearch();

		const Patches& getPatches() const
		{
			if (m_filter.empty() && !m_hideDuplicatesByHash && !m_hideDuplicatesByName)
				return m_patches;
			return m_filteredPatches;
		}

		pluginLib::patchDB::PatchPtr getPatch(const size_t _index) const;

		void filterPatches();
		void sortPatches();
		bool match(const Patch& _patch) const;

		pluginLib::patchDB::DataSourceNodePtr getDatasource() const;

		pluginLib::patchDB::SourceType getSourceType() const;

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

		void activateSelectedPatch() const;

		void openContextMenu(const Rml::Event& _event);

		static void showDeleteConfirmationMessageBox(genericUI::MessageBox::Callback _callback);

		void setFilter(const std::string& _filter);

		juceRmlUi::ElemList* getElement() const { return m_list; }

	private:
		bool exportPresets(bool _selectedOnly, const pluginLib::FileType& _fileType) const;

		void updateEntries() const;

		void setFilter(const std::string& _filter, bool _hideDuplicatesByHash, bool _hideDuplicatesByName);

		void onSelectionChanged() const;

		void onListKeyDown(Rml::Event& _event);

		PatchManagerUiRml& m_patchManager;
		juceRmlUi::ElemList* m_list;

		std::shared_ptr<pluginLib::patchDB::Search> m_search;
		Patches m_patches;
		Patches m_filteredPatches;
		std::string m_filter;
		bool m_hideDuplicatesByHash = false;
		bool m_hideDuplicatesByName = false;
		pluginLib::patchDB::SearchHandle m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
		bool m_activateOnSelectionChange = true;
	};
}
