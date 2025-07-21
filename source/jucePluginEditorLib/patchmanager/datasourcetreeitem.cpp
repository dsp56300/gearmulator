#include "datasourcetreeitem.h"

#include <cassert>

#include "listmodel.h"
#include "patchmanager.h"

#include "../pluginEditor.h"

#include "baseLib/filesystem.h"

#include "jucePluginLib/patchdb/datasource.h"
#include "jucePluginLib/patchdb/search.h"

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
			case pluginLib::patchDB::SourceType::LocalStorage:
				return _ds.name;
			case pluginLib::patchDB::SourceType::File:
			case pluginLib::patchDB::SourceType::Folder:
				{
					auto n = _ds.name;
					const auto idxA = n.find_first_of("\\/");
					const auto idxB = n.find_last_of("\\/");
					if(idxA != std::string::npos && idxB != std::string::npos && (idxB - idxA) > 1)
					{
						return n.substr(0, idxA+1) + ".." + n.substr(idxB);
					}
					return n;
				}
			default:
				assert(false);
				return"invalid";
			}
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

	DatasourceTreeItem::DatasourceTreeItem(PatchManagerUiJuce& _pm, const pluginLib::patchDB::DataSourceNodePtr& _ds) : TreeItem(_pm,{}), m_dataSource(_ds)
	{
		setTitle(getDataSourceNodeTitle(_ds));

		pluginLib::patchDB::SearchRequest sr;
		sr.sourceNode = _ds;
		search(std::move(sr));
	}

	bool DatasourceTreeItem::isInterestedInPatchList(const ListModel*, const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		return m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage;
	}

	void DatasourceTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		TreeItem::itemClicked(_mouseEvent);
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

		if (dsA->type != dsB->type)
			return static_cast<int>(dsA->type) - static_cast<int>(dsB->type);

		return TreeItem::compareElements(_a, _b);
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
