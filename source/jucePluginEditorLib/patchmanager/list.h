#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct Search;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class List : public juce::ListBox, juce::ListBoxModel
	{
	public:
		using Patch = pluginLib::patchDB::PatchPtr;
		using Patches = std::vector<Patch>;

		explicit List(PatchManager& _pm);

		void setContent(const pluginLib::patchDB::SearchHandle& _handle);

		// ListBoxModel
		int getNumRows() override;
		void paintListBoxItem(int _rowNumber, juce::Graphics& _g, int _width, int _height, bool _rowIsSelected) override;
		juce::var getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe) override;

		void selectedRowsChanged(int lastRowSelected) override;

		const Patches& getPatches() const
		{
			if (m_filter.empty())
				return m_patches;
			return m_filteredPatches;
		}

		Patch getPatch(const size_t _index) const
		{
			return getPatch(getPatches(), _index);
		}

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

		static Patch getPatch(const Patches& _patches, const size_t _index)
		{
			if (_index >= _patches.size())
				return {};
			return _patches[_index];
		}

		void setFilter(const std::string& _filter);

	private:
		void sortPatches();
		void filterPatches();
		bool match(const Patch& _patch) const;
		void setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search);

		PatchManager& m_patchManager;

		std::shared_ptr<pluginLib::patchDB::Search> m_search;
		Patches m_patches;
		Patches m_filteredPatches;
		std::string m_filter;
	};
}
