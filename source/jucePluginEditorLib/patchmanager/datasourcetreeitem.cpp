#include "datasourcetreeitem.h"

#include "../../jucePluginLib/patchdb/datasource.h"

namespace jucePluginEditorLib::patchManager
{
	namespace
	{
		std::string getTitle(const pluginLib::patchDB::DataSource& _ds)
		{
			switch(_ds.type)
			{
			case pluginLib::patchDB::SourceType::Invalid:
				return {};
			case pluginLib::patchDB::SourceType::Rom:
			case pluginLib::patchDB::SourceType::File:
			case pluginLib::patchDB::SourceType::Folder:
				return _ds.name;
//			default:
//				return {};
			}
			return {};
		}
	}

	DatasourceTreeItem::DatasourceTreeItem(PatchManager& _pm, const pluginLib::patchDB::DataSource& _ds) : TreeItem(_pm, getTitle(_ds))
	{
	}
}
