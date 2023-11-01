#include "datasourcetreeitem.h"

#include "../../jucePluginLib/patchdb/datasource.h"
#include "../../jucePluginLib/patchdb/search.h"

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
		pluginLib::patchDB::SearchRequest sr;
		sr.source = _ds;
		search(std::move(sr));
	}

	void DatasourceTreeItem::processSearchUpdated(const pluginLib::patchDB::Search& _search)
	{
		setTitle(getTitle(_search.request.source) + " (" + std::to_string(_search.getResultSize()) + ")");
	}
}
