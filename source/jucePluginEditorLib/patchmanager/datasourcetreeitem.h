#pragma once

#include "treeitem.h"

namespace pluginLib::patchDB
{
	struct DataSource;
}

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTreeItem : public TreeItem
	{
	public:
		DatasourceTreeItem(PatchManagerUiJuce& _pm, pluginLib::patchDB::DataSourceNodePtr _ds);

		const auto& getDataSource() const { return m_dataSource; }

		juce::String getTooltip() override;

		juce::var getDragSourceDescription() override;
	private:
		const pluginLib::patchDB::DataSourceNodePtr m_dataSource;
	};
}
