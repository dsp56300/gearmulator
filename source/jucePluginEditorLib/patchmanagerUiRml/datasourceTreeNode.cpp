#include "datasourceTreeNode.h"

#include "patchmanagerUiRml.h"

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/patchmanager/patchmanager.h"

#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInplaceEditor.h"
#include "juceRmlUi/rmlMenu.h"

#include "juceUiLib/messageBox.h"

namespace jucePluginEditorLib::patchManagerRml
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

	std::string DatasourceNode::getText() const
	{
		return m_ds ? getDataSourceNodeTitle(m_ds) : std::string();
	}

	DatasourceTreeElem::DatasourceTreeElem(Tree& _tree, const Rml::String& _tag) : TreeElem(_tree, _tag)
	{
	}

	void DatasourceTreeElem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeElem::setNode(_node);

		auto* item = dynamic_cast<DatasourceNode*>(_node.get());
		if (!item)
			return;

		m_dataSource = item->getDatasource();
		if (!m_dataSource)
			return;

		const auto name = getDataSourceNodeTitle(m_dataSource);

		setName(name);

		pluginLib::patchDB::SearchRequest sr;
		sr.sourceNode = m_dataSource;
		search(std::move(sr));
	}

	void DatasourceTreeElem::onRightClick(const Rml::Event& _event)
	{
		TreeElem::onRightClick(_event);

		juceRmlUi::Menu menu;

		menu.addEntry("Refresh", [this]
		{
			getDB().refreshDataSource(m_dataSource);
		});

		if(m_dataSource->type == pluginLib::patchDB::SourceType::File || 
			m_dataSource->type == pluginLib::patchDB::SourceType::Folder ||
			m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			menu.addEntry("Remove", [this]
			{
				getDB().removeDataSource(*m_dataSource);
			});
			menu.addEntry("Reveal in File Browser", [this]
			{
				if(m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
				{
					getDB().getLocalStorageFile(*m_dataSource).revealToUser();
				}
				else
				{
					juce::File(m_dataSource->name).revealToUser();
				}
			});
		}
		if(m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			menu.addEntry("Delete", [this]
			{
				genericUI::MessageBox::showYesNo(juce::MessageBoxIconType::WarningIcon, 
					"Patch Manager", 
					"Are you sure that you want to delete your user bank named '" + m_dataSource->name + "'?",
					[this](const genericUI::MessageBox::Result _result)
					{
						if (_result == genericUI::MessageBox::Result::Yes)
						{
							getDB().removeDataSource(*m_dataSource);
						}
					});
			});
		}
		if(m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			menu.addEntry("Rename...", [this]
			{
				new juceRmlUi::InplaceEditor(this, m_dataSource->name,
				[this](const std::string& _newName)
				{
					getDB().renameDataSource(m_dataSource, _newName);
				});
			});

			menu.addSubMenu("Export...", getPatchManager().getEditor().createExportFileTypeMenu([this](const pluginLib::FileType& _fileType)
			{
				const auto s = getDB().getSearch(getSearchHandle());
				if(s)
				{
					std::vector<pluginLib::patchDB::PatchPtr> patches(s->results.begin(), s->results.end());
					getDB().exportPresets(std::move(patches), _fileType);
				}
			}));
		}

		if(m_dataSource->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			const auto clipboardPatches = getDB().getPatchesFromClipboard();

			if(!clipboardPatches.empty())
			{
				menu.addSeparator();
				menu.addEntry("Paste from Clipboard", [this, clipboardPatches]
				{
					getDB().copyPatchesToLocalStorage(m_dataSource, clipboardPatches, -1);
				});
			}
		}

		menu.runModal(this, juceRmlUi::helper::getMousePos(_event));
	}
}
