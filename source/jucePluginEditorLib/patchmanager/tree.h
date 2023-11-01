#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "types.h"

namespace pluginLib::patchDB
{
	struct Dirty;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;
	class GroupTreeItem;

	class Tree : public juce::TreeView
	{
	public:
		Tree(PatchManager& _patchManager);
		~Tree() override;

		void updateDataSources();
		void updateCategories();
		void updateTags();

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

	private:
		void addGroup(GroupType _type);
		GroupTreeItem* getItem(GroupType _type);

		PatchManager& m_patchManager;
		std::map<GroupType, GroupTreeItem*> m_groupItems;
	};
}
