#pragma once

#include "treeitem.h"

#include "jucePluginLib/patchdb/datasource.h"

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

		bool isInterestedInPatchList(const ListModel* _list, const std::vector<pluginLib::patchDB::PatchPtr>& _patches) override;

		bool isInterestedInFileDrag(const juce::StringArray& files) override;

		void patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const SavePatchDesc* _savePatchDesc = nullptr) override;

		void itemClicked(const juce::MouseEvent&) override;
		void refresh();

		int compareElements(const TreeViewItem* _a, const TreeViewItem* _b) override;

		bool beginEdit() override;

		const auto& getDataSource() const { return m_dataSource; }

		juce::String getTooltip() override;

		juce::var getDragSourceDescription() override;
	private:
		const pluginLib::patchDB::DataSourceNodePtr m_dataSource;
	};
}
