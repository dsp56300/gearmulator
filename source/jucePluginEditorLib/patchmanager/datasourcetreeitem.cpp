#include "datasourcetreeitem.h"

#include "listmodel.h"
#include "patchmanager.h"

#include "../pluginEditor.h"

#include "jucePluginLib/patchdb/datasource.h"

namespace jucePluginEditorLib::patchManager
{
	DatasourceTreeItem::DatasourceTreeItem(PatchManagerUiJuce& _pm, pluginLib::patchDB::DataSourceNodePtr _ds) : TreeItem(_pm,{}), m_dataSource(std::move(_ds))
	{
	}

	juce::String DatasourceTreeItem::getTooltip()
	{
		const auto& ds = getDataSource();
		if(!ds)
			return {};
		switch (ds->type)
		{
		case pluginLib::patchDB::SourceType::Invalid:
		case pluginLib::patchDB::SourceType::Rom:
		case pluginLib::patchDB::SourceType::Count:
			return{};
		default:
			return ds->name;
		}
	}

	juce::var DatasourceTreeItem::getDragSourceDescription()
	{
		return {};
	}
}
