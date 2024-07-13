#pragma once

#include "treeitem.h"
#include "types.h"

namespace jucePluginEditorLib::patchManager
{
	class TagTreeItem : public TreeItem
	{
	public:
		TagTreeItem(PatchManager& _pm, GroupType _type, const std::string& _tag);

		bool mightContainSubItems() override
		{
			return false;
		}

		auto getGroupType() const { return m_group; }

		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
		bool isInterestedInPatchList(const ListModel* _list, const std::vector<pluginLib::patchDB::PatchPtr>& _indices) override;
		void patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const SavePatchDesc* _savePatchDesc = nullptr) override;
		void onParentSearchChanged(const pluginLib::patchDB::SearchRequest& _parentSearchRequest) override;

		static void modifyTags(PatchManager& _pm, pluginLib::patchDB::TagType _type, const std::string& _tag, const std::vector<pluginLib::patchDB::PatchPtr>& _patches);

		const auto& getTag() const { return m_tag; }

		void itemClicked(const juce::MouseEvent&) override;

		pluginLib::patchDB::Color getColor() const override;
	private:
		const GroupType m_group;
		const std::string m_tag;
	};
}
