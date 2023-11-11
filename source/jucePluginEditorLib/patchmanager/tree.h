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
		void updateTags(GroupType _type);
		void updateTags(pluginLib::patchDB::TagType _type);

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

		void paint(juce::Graphics& g) override;

		bool keyPressed(const juce::KeyPress& _key) override;

		void setFilter(const std::string& _filter);

	private:
		void addGroup(GroupType _type);
		GroupTreeItem* getItem(GroupType _type);

		PatchManager& m_patchManager;
		std::map<GroupType, GroupTreeItem*> m_groupItems;
		std::string m_filter;
	};
}
