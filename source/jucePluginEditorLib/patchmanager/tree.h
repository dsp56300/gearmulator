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
	class PatchManagerUiJuce;
	class DatasourceTreeItem;
	class GroupTreeItem;

	class Tree : public juce::TreeView
	{
	public:
		Tree(PatchManagerUiJuce& _patchManager);
		~Tree() override;

		void updateTags(GroupType _type);
		void updateTags(pluginLib::patchDB::TagType _type);

		bool keyPressed(const juce::KeyPress& _key) override;

		void setFilter(const std::string& _filter);

		DatasourceTreeItem* getItem(const pluginLib::patchDB::DataSource& _ds);

		virtual void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _searchRequest);

		void addGroup(GroupType _type, const std::string& _name);

		GroupTreeItem* getItem(GroupType _type);
	protected:
		void addGroup(GroupType _type);

	private:
		PatchManagerUiJuce& m_patchManager;
		std::map<GroupType, GroupTreeItem*> m_groupItems;
		std::string m_filter;
	};
}
