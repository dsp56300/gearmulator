#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct Search;
	struct SearchRequest;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class TreeItem : public juce::TreeViewItem
	{
	public:
		TreeItem(PatchManager& _patchManager, std::string _title);

		PatchManager& getPatchManager() const { return m_patchManager; }

		void setTitle(const std::string& _title);

		auto getSearchHandle() const { return m_searchHandle; }

		virtual void processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _dirtySearches);

		void itemSelectionChanged(bool _isNowSelected) override;

		void itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex) override;
		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

	protected:
		void search(pluginLib::patchDB::SearchRequest&& _request);
		virtual void processSearchUpdated(const pluginLib::patchDB::Search& _search) {};

	private:
		bool mightContainSubItems() override
		{
			return true;
		}

		void paintItem(juce::Graphics& _g, int _width, int _height) override;

		PatchManager& m_patchManager;
		std::string m_title;
		uint32_t m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	};
}
