#pragma once

#include "editable.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "../types.h"

namespace pluginLib::patchDB
{
	struct SearchRequest;
	struct PatchKey;
	struct Search;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class List : public juce::ListBox, juce::ListBoxModel, Editable
	{
	public:
		using Patch = pluginLib::patchDB::PatchPtr;
		using Patches = std::vector<Patch>;

		explicit List(PatchManager& _pm);

		void setContent(const pluginLib::patchDB::SearchHandle& _handle);
		void setContent(pluginLib::patchDB::SearchRequest&& _request);

		void refreshContent();

		// ListBoxModel
		int getNumRows() override;
		void paintListBoxItem(int _rowNumber, juce::Graphics& _g, int _width, int _height, bool _rowIsSelected) override;
		juce::var getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe) override;
		Component* refreshComponentForRow(int rowNumber, bool isRowSelected, Component* existingComponentToUpdate) override;

		void selectedRowsChanged(int lastRowSelected) override;

		const Patches& getPatches() const
		{
			if (m_filter.empty() && !m_hideDuplicates)
				return m_patches;
			return m_filteredPatches;
		}

		Patch getPatch(const size_t _index) const
		{
			return getPatch(getPatches(), _index);
		}

		std::set<Patch> getSelectedPatches() const;
		bool setSelectedPatches(const std::set<Patch>& _patches);
		bool setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches);

		void activateSelectedPatch() const;

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

		static std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);

		pluginLib::patchDB::DataSourceNodePtr getDataSource() const;

		static Patch getPatch(const Patches& _patches, const size_t _index)
		{
			if (_index >= _patches.size())
				return {};
			return _patches[_index];
		}

		void setFilter(const std::string& _filter);
		void setFilter(const std::string& _filter, bool _hideDuplicates);

		PatchManager& getPatchManager() const
		{
			return m_patchManager;
		}

		static void sortPatches(Patches& _patches, pluginLib::patchDB::SourceType _sourceType);
		void listBoxItemClicked(int _row, const juce::MouseEvent&) override;
		void backgroundClicked(const juce::MouseEvent&) override;

		static bool showDeleteConfirmationMessageBox();
		pluginLib::patchDB::SourceType getSourceType() const;
		bool canReorderPatches() const;
		bool hasTagFilters() const;
		bool hasFilters() const;

		pluginLib::patchDB::SearchHandle getSearchHandle() const;

	private:
		void sortPatches();
		void sortPatches(Patches& _patches) const;
		void filterPatches();
		bool match(const Patch& _patch) const;
		void setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search);
		bool exportPresets(bool _selectedOnly, FileType _fileType) const;
		bool onClicked(const juce::MouseEvent&);
		void cancelSearch();

		PatchManager& m_patchManager;

		std::shared_ptr<pluginLib::patchDB::Search> m_search;
		Patches m_patches;
		Patches m_filteredPatches;
		std::string m_filter;
		bool m_hideDuplicates = false;
		pluginLib::patchDB::SearchHandle m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
		bool m_ignoreSelectedRowsChanged = false;
	};
}
