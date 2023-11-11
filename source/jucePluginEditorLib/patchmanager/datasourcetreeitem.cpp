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
			if (_ds->origin == pluginLib::patchDB::DataSourceOrigin::Manual)
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

	void DatasourceTreeItem::refresh()
	{
		setTitle(getDataSourceNodeTitle(m_dataSource));
	}

	int DatasourceTreeItem::compareElements(const TreeViewItem* _a, const TreeViewItem* _b)
	{
		const auto* a = dynamic_cast<const DatasourceTreeItem*>(_a);
		const auto* b = dynamic_cast<const DatasourceTreeItem*>(_b);

		if (!a || !b)
			return TreeItem::compareElements(_a, _b);

		const auto& dsA = a->m_dataSource;
		const auto& dsB = b->m_dataSource;

		if (dsA->type == pluginLib::patchDB::SourceType::Rom || dsB->type == pluginLib::patchDB::SourceType::Rom)
			int foo = 0;

		if (dsA->type != dsB->type)
			return static_cast<int>(dsA->type) - static_cast<int>(dsB->type);

		return TreeItem::compareElements(_a, _b);
	}
}
