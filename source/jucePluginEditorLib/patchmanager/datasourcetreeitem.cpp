#include "datasourcetreeitem.h"

#include <cassert>

#include "patchmanager.h"
#include "../pluginEditor.h"

#include "../../jucePluginLib/patchdb/datasource.h"
#include "../../jucePluginLib/patchdb/search.h"

#include "../../synthLib/buildconfig.h"

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
			case pluginLib::patchDB::SourceType::LocalStorage:
				return _ds.name;
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

	DatasourceTreeItem::DatasourceTreeItem(PatchManager& _pm, const pluginLib::patchDB::DataSourceNodePtr& _ds) : TreeItem(_pm,{}), m_dataSource(_ds)
	{
		setTitle(getDataSourceNodeTitle(_ds));

		pluginLib::patchDB::SearchRequest sr;
		sr.sourceNode = _ds;
		search(std::move(sr));
	}

	bool DatasourceTreeItem::isInterestedInSavePatchDesc(const SavePatchDesc& _desc)
	{
		return m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage;
	}

	bool DatasourceTreeItem::isInterestedInPatchList(const List* _list, const juce::Array<juce::var>& _indices)
	{
		return m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage;
	}

	void DatasourceTreeItem::patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		TreeItem::patchesDropped(_patches);

		if (m_dataSource->type != pluginLib::patchDB::SourceType::LocalStorage)
			return;

		if (juce::ModifierKeys::currentModifiers.isShiftDown())
		{
			if(List::showDeleteConfirmationMessageBox())
				getPatchManager().removePatches(m_dataSource, _patches);
		}
		else
		{
#if SYNTHLIB_DEMO_MODE
			getPatchManager().getEditor().showDemoRestrictionMessageBox();
#else
			getPatchManager().copyPatchesTo(m_dataSource, _patches);
#endif
		}
	}

	void DatasourceTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!_mouseEvent.mods.isPopupMenu())
			return;

		juce::PopupMenu menu;

		menu.addItem("Refresh", [this]
		{
			getPatchManager().refreshDataSource(m_dataSource);
		});

		if(m_dataSource->type == pluginLib::patchDB::SourceType::File || 
			m_dataSource->type == pluginLib::patchDB::SourceType::Folder ||
			m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			menu.addItem("Remove", [this]
			{
				getPatchManager().removeDataSource(*m_dataSource);
			});
		}
		if(m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			menu.addItem("Delete", [this]
			{
				if(1 == juce::NativeMessageBox::showYesNoBox(juce::AlertWindow::WarningIcon, 
					"Patch Manager", 
					"Are you sure that you want to delete your user bank named '" + getDataSource()->name + "'?"))
					getPatchManager().removeDataSource(*m_dataSource);
			});
		}
		if(m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			menu.addItem("Rename...", [this]
			{
				beginEdit();
			});

			auto fileTypeMenu = [this](const std::function<void(FileType)>& _func)
			{
				juce::PopupMenu menu;
				menu.addItem(".syx", [this, _func]{_func(FileType::Syx);});
				menu.addItem(".mid", [this, _func]{_func(FileType::Mid);});
				return menu;
			};

			menu.addSubMenu("Export...", fileTypeMenu([this](const FileType _fileType)
			{
				const auto s = getPatchManager().getSearch(getSearchHandle());
				if(s)
				{
					std::vector patches(s->results.begin(), s->results.end());
					getPatchManager().exportPresets(std::move(patches), _fileType);
				}
			}));
		}
		menu.showMenuAsync({});
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

	bool DatasourceTreeItem::beginEdit()
	{
		if(m_dataSource->type != pluginLib::patchDB::SourceType::LocalStorage)
			return TreeItem::beginEdit();

		static_cast<TreeItem&>(*this).beginEdit(m_dataSource->name, [this](bool _success, const std::string& _newName)
		{
			if(_newName != m_dataSource->name)
			{
				getPatchManager().renameDataSource(m_dataSource, _newName);
			}
		});
		return true;
	}
}
