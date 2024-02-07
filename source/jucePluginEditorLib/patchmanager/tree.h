#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

#include "types.h"

namespace pluginLib::patchDB
{
	struct SearchRequest;
	struct DataSource;
	struct Dirty;
}

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem;
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

		virtual void processDirty(const pluginLib::patchDB::Dirty& _dirty);

		void paint(juce::Graphics& g) override;

		bool keyPressed(const juce::KeyPress& _key) override;

		void setFilter(const std::string& _filter);

		DatasourceTreeItem* getItem(const pluginLib::patchDB::DataSource& _ds);

		virtual void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest);

		void addGroup(GroupType _type, const std::string& _name);

		GroupTreeItem* getItem(GroupType _type);
	protected:
		void addGroup(GroupType _type);

	private:
		PatchManager& m_patchManager;
		std::map<GroupType, GroupTreeItem*> m_groupItems;
		std::string m_filter;
	};
}
