#include "datasourcetreeitem.h"

#include "../../jucePluginLib/patchdb/datasource.h"
#include "../../jucePluginLib/patchdb/search.h"

namespace jucePluginEditorLib::patchManager
{
	namespace
	{
		std::string getTitle(const pluginLib::patchDB::DataSource& _ds)
		{
			switch (_ds.type)
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

	DatasourceTreeItem::DatasourceTreeItem(PatchManager& _pm, const pluginLib::patchDB::DataSourcePtr& _ds) : TreeItem(_pm,{}), m_dataSource(_ds)
	{
		DatasourceTreeItem::setTitle(getTitle(*_ds));

		pluginLib::patchDB::SearchRequest sr;
		sr.source = *_ds;
		search(std::move(sr));
	}

	void DatasourceTreeItem::setTitle(const std::string& _title)
	{
		if(!m_dataSource->parent)
		{
			TreeItem::setTitle(_title);
			return;
		}

		const auto pos = _title.find_last_of("\\/");
		if (pos != std::string::npos)
			TreeItem::setTitle(_title.substr(pos + 1));
		else
			TreeItem::setTitle(_title);
	}

	void DatasourceTreeItem::processSearchUpdated(const pluginLib::patchDB::Search& _search)
	{
		setTitle(getTitle(_search.request.source) + " (" + std::to_string(_search.getResultSize()) + ")");
	}
}
