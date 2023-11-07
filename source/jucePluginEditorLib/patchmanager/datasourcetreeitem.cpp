#include "datasourcetreeitem.h"

#include "patchmanager.h"

#include "../../jucePluginLib/patchdb/datasource.h"
#include "../../jucePluginLib/patchdb/search.h"

namespace jucePluginEditorLib::patchManager
{
	namespace
	{
		std::string getDataSourceTitle(const pluginLib::patchDB::DataSource& _ds)
		{
			switch (_ds.type)
			{
			case pluginLib::patchDB::SourceType::Invalid:
			case pluginLib::patchDB::SourceType::Count:
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

		std::string getDataSourceNodeTitle(const pluginLib::patchDB::DataSourceNodePtr& _ds)
		{
			if (!_ds->hasParent())
				return getDataSourceTitle(*_ds);

			auto t = getDataSourceTitle(*_ds);
			const auto pos = t.find_last_of("\\/");
			if (pos != std::string::npos)
				return t.substr(pos + 1);
			return t;
		}
	}

	DatasourceTreeItem::DatasourceTreeItem(PatchManager& _pm, const pluginLib::patchDB::DataSourceNodePtr& _ds) : TreeItem(_pm,{}), m_dataSource(_ds)
	{
		setTitle(getDataSourceNodeTitle(_ds));

		pluginLib::patchDB::SearchRequest sr;
		sr.source = static_cast<pluginLib::patchDB::DataSource>(*_ds);
		search(std::move(sr));
	}

	void DatasourceTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(_mouseEvent.mods.isPopupMenu())
		{
			juce::PopupMenu menu;

			menu.addItem("Refresh", [this]
			{
				getPatchManager().refreshDataSource(m_dataSource);
			});

			if(m_dataSource->type == pluginLib::patchDB::SourceType::File || m_dataSource->type == pluginLib::patchDB::SourceType::Folder)
			{
				menu.addItem("Remove", [this]
				{
					getPatchManager().removeDataSource(*m_dataSource);
				});
			}
			menu.showMenuAsync({});
		}
	}
}
