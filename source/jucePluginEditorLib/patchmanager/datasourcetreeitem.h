#pragma once
#include "treeitem.h"
#include "../../jucePluginLib/patchdb/datasource.h"

namespace pluginLib::patchDB
{
	struct DataSource;
}

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem : public TreeItem
	{
	public:
		DatasourceTreeItem(PatchManager& _pm, const pluginLib::patchDB::DataSourceNodePtr& _ds);

		bool mightContainSubItems() override
		{
			return m_dataSource->type == pluginLib::patchDB::SourceType::Folder;
		}

		bool isInterestedInSavePatchDesc(const SavePatchDesc& _desc) override;
		bool isInterestedInPatchList(const List* _list, const juce::Array<juce::var>& _indices) override;

		void patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches) override;

		void itemClicked(const juce::MouseEvent&) override;
		void refresh();

		int compareElements(const TreeViewItem* _a, const TreeViewItem* _b) override;

		bool beginEdit() override;

		const auto& getDataSource() const { return m_dataSource; }

	private:
		const pluginLib::patchDB::DataSourceNodePtr m_dataSource;
	};
}
