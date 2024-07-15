#include "datasourcetreeitem.h"

#include <cassert>

#include "listmodel.h"
#include "patchmanager.h"

#include "../pluginEditor.h"

#include "jucePluginLib/patchdb/datasource.h"
#include "jucePluginLib/patchdb/search.h"

#include "synthLib/buildconfig.h"
#include "synthLib/os.h"

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

	DatasourceTreeItem::DatasourceTreeItem(PatchManager& _pm, const pluginLib::patchDB::DataSourceNodePtr& _ds) : TreeItem(_pm,{}), m_dataSource(_ds)
	{
		setTitle(getDataSourceNodeTitle(_ds));

		pluginLib::patchDB::SearchRequest sr;
		sr.sourceNode = _ds;
		search(std::move(sr));
	}

	bool DatasourceTreeItem::isInterestedInPatchList(const ListModel* _list, const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		return m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage;
	}

	bool DatasourceTreeItem::isInterestedInFileDrag(const juce::StringArray& files)
	{
		if(m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
			return true;

		return TreeItem::isInterestedInFileDrag(files);
	}

	void DatasourceTreeItem::patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const SavePatchDesc* _savePatchDesc/* = nullptr*/)
	{
		TreeItem::patchesDropped(_patches);

		if (m_dataSource->type != pluginLib::patchDB::SourceType::LocalStorage)
			return;

		if (juce::ModifierKeys::currentModifiers.isShiftDown())
		{
			if(ListModel::showDeleteConfirmationMessageBox())
				getPatchManager().removePatches(m_dataSource, _patches);
		}
		else
		{
#if SYNTHLIB_DEMO_MODE
			getPatchManager().getEditor().showDemoRestrictionMessageBox();
#else
			const int part = _savePatchDesc ? _savePatchDesc->getPart() : -1;

			getPatchManager().copyPatchesToLocalStorage(m_dataSource, _patches, part);
#endif
		}
	}

	void DatasourceTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!_mouseEvent.mods.isPopupMenu())
		{
			TreeItem::itemClicked(_mouseEvent);
			return;
		}

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
					std::vector<pluginLib::patchDB::PatchPtr> patches(s->results.begin(), s->results.end());
					getPatchManager().exportPresets(std::move(patches), _fileType);
				}
			}));
		}

		if(m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			const auto clipboardPatches = getPatchManager().getPatchesFromClipboard();

			if(!clipboardPatches.empty())
			{
				menu.addSeparator();
				menu.addItem("Paste from Clipboard", [this, clipboardPatches]
				{
					getPatchManager().copyPatchesToLocalStorage(m_dataSource, clipboardPatches, -1);
				});
			}
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
		if(!m_dataSource || m_dataSource->patches.empty())
			return TreeItem::getDragSourceDescription();

		std::vector<pluginLib::patchDB::PatchPtr> patchesVec{m_dataSource->patches.begin(), m_dataSource->patches.end()};

		pluginLib::patchDB::DataSource::sortByProgram(patchesVec);

		uint32_t i=0;
		std::map<uint32_t, pluginLib::patchDB::PatchPtr> patchesMap;

		for (const auto& patch : patchesVec)
			patchesMap.insert({i++, patch});

		return new SavePatchDesc(getPatchManager(), std::move(patchesMap), synthLib::getFilenameWithoutPath(m_dataSource->name));
	}
}
