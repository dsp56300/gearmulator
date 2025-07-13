#include "listmodel.h"

#include "listitem.h"
#include "patchmanagerUiRml.h"

#include "jucePluginEditorLib/pluginEditor.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"
#include "jucePluginEditorLib/patchmanager/search.h"

#include "juceRmlUi/rmlElemList.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInplaceEditor.h"
#include "juceRmlUi/rmlMenu.h"

#include "juceUiLib/messageBox.h"

namespace jucePluginEditorLib::patchManagerRml
{
	namespace
	{
		Rml::ElementInstancerGeneric<ListElemEntry> g_instancer;
	}

	ListModel::ListModel(PatchManagerUiRml& _pm, juceRmlUi::ElemList* _list) : m_patchManager(_pm), m_list(_list)
	{
		_list->setInstancer(&g_instancer);

		auto& list = _list->getList();

		list.setMultiselect(true);
		list.evSelectionChanged.addListener([this](juceRmlUi::List* const&) { onSelectionChanged(); });
	}

	PatchManagerUiRml& ListModel::getPatchManager() const
	{
		return m_patchManager;
	}

	patchManager::PatchManager& ListModel::getDB() const
	{
		return m_patchManager.getDB();
	}

	pluginLib::patchDB::SearchHandle ListModel::getSearchHandle() const
	{
		if (!m_search)
			return pluginLib::patchDB::g_invalidSearchHandle;
		return m_search->handle;
	}

	std::set<ListModel::Patch> ListModel::getSelectedPatches() const
	{
		auto indices = getSelectedEntries();

		if (indices.empty())
			return {};

		std::set<Patch> result;

		const auto& patches = getPatches();

		for (size_t index : indices)
			result.insert(patches[index]);

		return result;
	}

	bool ListModel::setSelectedPatches(const std::set<Patch>& _selection)
	{
		if (_selection.empty())
		{
			m_list->getList().deselectAll();
			return false;
		}

		std::set<pluginLib::patchDB::PatchKey> patchKeys;

		for (const auto& patch : _selection)
			patchKeys.insert(pluginLib::patchDB::PatchKey(*patch));

		return setSelectedPatches(patchKeys);
	}

	bool ListModel::setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _selection)
	{
		if (_selection.empty())
		{
			m_list->getList().deselectAll();
			return false;
		}

		const auto& patches = getPatches();

		std::vector<size_t> indices;

		for (size_t i=0; i<patches.size(); ++i)
		{
			const pluginLib::patchDB::PatchKey key(*patches[i]);

			if (_selection.find(key) != _selection.end())
				indices.push_back(i);
		}

		setSelectedEntries(indices);

		return !indices.empty();
	}

	std::vector<size_t> ListModel::getSelectedEntries() const
	{
		return m_list->getList().getSelectedIndices();
	}

	void ListModel::setSelectedEntries(const std::vector<size_t>& _indices)
	{
		m_activateOnSelectionChange = false;
		m_list->getList().setSelectedIndices(_indices);
		m_activateOnSelectionChange = true;
	}

	void ListModel::setContent(pluginLib::patchDB::SearchHandle _searchHandle)
	{
		cancelSearch();

		const auto& search = m_patchManager.getDB().getSearch(_searchHandle);

		if (!search)
			return;

		setContent(search);
	}

	void ListModel::setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search)
	{
		const std::set<Patch> selectedPatches = getSelectedPatches();

		m_search = _search;

		m_patches.clear();
		{
			std::shared_lock lock(_search->resultsMutex);
			m_patches.insert(m_patches.end(), _search->results.begin(), _search->results.end());
		}

		sortPatches();
		filterPatches();

		updateEntries();

		setSelectedPatches(selectedPatches);

		getPatchManager().setListStatus(static_cast<uint32_t>(selectedPatches.size()), static_cast<uint32_t>(getPatches().size()));
	}

	void ListModel::setContent(pluginLib::patchDB::SearchRequest&& _search)
	{
		cancelSearch();
		const auto sh = getDB().search(std::move(_search));
		setContent(sh);
		m_searchHandle = sh;
	}

	void ListModel::cancelSearch()
	{
		if(m_searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return;
		getDB().cancelSearch(m_searchHandle);
		m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	}

	void ListModel::filterPatches()
	{
		if (m_filter.empty() && !m_hideDuplicatesByHash && !m_hideDuplicatesByName)
		{
			m_filteredPatches.clear();
			return;
		}

		m_filteredPatches.reserve(m_patches.size());
		m_filteredPatches.clear();

		std::set<pluginLib::patchDB::PatchHash> knownHashes;
		std::set<std::string> knownNames;

		for (const auto& patch : m_patches)
		{
			if(m_hideDuplicatesByHash)
			{
				if(knownHashes.find(patch->hash) != knownHashes.end())
					continue;
				knownHashes.insert(patch->hash);
			}

			if(m_hideDuplicatesByName)
			{
				if(knownNames.find(patch->getName()) != knownNames.end())
					continue;
				knownNames.insert(patch->getName());
			}

			if (m_filter.empty() || match(patch))
				m_filteredPatches.emplace_back(patch);
		}
	}

	void ListModel::sortPatches()
	{
		patchManager::PatchManagerUi::sortPatches(m_patches, getSourceType());
	}

	bool ListModel::match(const Patch& _patch) const
	{
		const auto name = _patch->getName();
		const auto t = patchManager::Search::lowercase(name);
		return t.find(m_filter) != std::string::npos;
	}

	pluginLib::patchDB::SourceType ListModel::getSourceType() const
	{
		if(!m_search)
			return pluginLib::patchDB::SourceType::Invalid;
		return m_search->getSourceType();
	}

	void ListModel::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		if (!m_search)
			return;

		if (_dirty.searches.empty())
			return;

		if(_dirty.searches.find(m_search->handle) != _dirty.searches.end())
			setContent(m_search);
	}

	void ListModel::activateSelectedPatch() const
	{
		const auto patches = getSelectedPatches();

		if(patches.size() == 1)
			getDB().setSelectedPatch(*patches.begin(), m_search->handle);
	}

	void ListModel::openContextMenu(const Rml::Event& _event)
	{
		const auto selectedPatches = getSelectedPatches();

		const auto hasSelectedPatches = !selectedPatches.empty();

		auto& editor = m_patchManager.getEditor();

		juceRmlUi::Menu menu;
		if(hasSelectedPatches)
			menu.addSubMenu("Export selected...", editor.createExportFileTypeMenu([this](const pluginLib::FileType& _fileType) { exportPresets(true, _fileType); }));
		menu.addSubMenu("Export all...", editor.createExportFileTypeMenu([this](const pluginLib::FileType& _fileType) { exportPresets(false, _fileType); }));

		if(hasSelectedPatches)
		{
			menu.addSeparator();

			pluginLib::patchDB::TypedTags tags;

			for (const auto& selectedPatch : selectedPatches)
				tags.add(selectedPatch->getTags());

			if(selectedPatches.size() == 1)
			{
				const auto& patch = *selectedPatches.begin();

				const auto& listEntry = m_list->getList().getEntry(getSelectedEntries().front());

				const auto elem = listEntry->getElement();

				if (elem)
				{
					menu.addEntry("Rename...", [this, patch, elem]
					{
						new juceRmlUi::InplaceEditor(elem, patch->getName(), [this, patch](const std::string& _newName)
						{
							getDB().renamePatch(patch, _newName);
						});
					});
				}

				menu.addEntry("Locate", [this, patch]
				{
					m_patchManager.setSelectedDataSource(patch->source.lock());
				});
			}

			if(!m_search->request.tags.empty())
			{
				menu.addEntry("Remove selected", [this, s = selectedPatches]
				{
					const std::vector<pluginLib::patchDB::PatchPtr> patches(s.begin(), s.end());
					pluginLib::patchDB::TypedTags removeTags;

					// converted "added" tags to "removed" tags
					for (const auto& tags : m_search->request.tags.get())
					{
						const pluginLib::patchDB::TagType type = tags.first;
						const auto& t = tags.second;
							
						for (const auto& tag : t.getAdded())
							removeTags.addRemoved(type, tag);
					}

					getDB().modifyTags(patches, removeTags);
//					m_patchManager.repaint();
				});
			}
			else if(getSourceType() == pluginLib::patchDB::SourceType::LocalStorage)
			{
				menu.addEntry("Delete selected", [this, s = selectedPatches]
				{
					showDeleteConfirmationMessageBox([this, s](genericUI::MessageBox::Result _result)
					{
						if (_result == genericUI::MessageBox::Result::Yes)
						{
							const std::vector<pluginLib::patchDB::PatchPtr> patches(s.begin(), s.end());
							getDB().removePatches(m_search->request.sourceNode, patches);
						}
					});
				});
			}

			if(tags.containsAdded())
			{
				bool haveSeparator = false;

				for (const auto& it : tags.get())
				{
					const auto type = it.first;

					const auto& t = it.second;

					if(t.empty())
						continue;

					const auto tagTypeName = getDB().getTagTypeName(type);

					if(tagTypeName.empty())
						continue;

					juceRmlUi::Menu tagMenu;

					for (const auto& tag : t.getAdded())
					{
						pluginLib::patchDB::TypedTags removeTags;
						removeTags.addRemoved(type, tag);

						std::vector<pluginLib::patchDB::PatchPtr> patches{selectedPatches.begin(), selectedPatches.end()};

						tagMenu.addEntry(tag, [this, s = std::move(patches), removeTags]
						{
							getDB().modifyTags(s, removeTags);
						});
					}

					if(!haveSeparator)
					{
						menu.addSeparator();
						haveSeparator = true;
					}

					menu.addSubMenu("Remove from " + tagTypeName, std::move(tagMenu));
				}
			}

			{
				bool haveSeparator = false;

				for(uint32_t i=0; i<static_cast<uint32_t>(pluginLib::patchDB::TagType::Count); ++i)
				{
					const auto type = static_cast<pluginLib::patchDB::TagType>(i);
					std::set<pluginLib::patchDB::Tag> availTags;
					getDB().getTags(type, availTags);

					if(availTags.empty())
						continue;

					const auto tagTypeName = getDB().getTagTypeName(type);

					if(tagTypeName.empty())
						continue;

					juceRmlUi::Menu tagMenu;

					for (const auto& tag : availTags)
					{
						pluginLib::patchDB::TypedTags addedTags;
						addedTags.add(type, tag);

						std::vector<pluginLib::patchDB::PatchPtr> patches{selectedPatches.begin(), selectedPatches.end()};

						tagMenu.addEntry(tag, [this, addedTags, s = std::move(patches)]
						{
							getDB().modifyTags(s, addedTags);
						});
					}

					if(!haveSeparator)
					{
						menu.addSeparator();
						haveSeparator = true;
					}

					menu.addSubMenu("Add to " + tagTypeName, std::move(tagMenu));
				}
			}
		}
		menu.addSeparator();
		menu.addEntry("Hide duplicates (by hash)", m_hideDuplicatesByHash, [this]
		{
			setFilter(m_filter, !m_hideDuplicatesByHash, m_hideDuplicatesByName);
		});
		menu.addEntry("Hide duplicates (by name)", m_hideDuplicatesByName, [this]
		{
			setFilter(m_filter, m_hideDuplicatesByHash, !m_hideDuplicatesByName);
		});

		menu.addSeparator();

		juceRmlUi::Menu layoutMenu;
		layoutMenu.addEntry("List + Info", m_patchManager.getLayout() == PatchManagerUiRml::LayoutType::List, [this]
		{
			m_patchManager.setLayout(PatchManagerUiRml::LayoutType::List);
		});
		layoutMenu.addEntry("Grid", m_patchManager.getLayout() == PatchManagerUiRml::LayoutType::Grid, [this]
		{
			m_patchManager.setLayout(PatchManagerUiRml::LayoutType::Grid);
		});
		menu.addSubMenu("Layout", std::move(layoutMenu));

		menu.runModal(_event.GetTargetElement(), juceRmlUi::helper::getMousePos(_event));
	}

	bool ListModel::exportPresets(const bool _selectedOnly, const pluginLib::FileType& _fileType) const
	{
		Patches patches;

		if(_selectedOnly)
		{
			const auto selected = getSelectedPatches();
			if(selected.empty())
				return false;
			patches.assign(selected.begin(), selected.end());
		}
		else
		{
			patches = getPatches();
		}

		if(patches.empty())
			return false;

		return getDB().exportPresets(std::move(patches), _fileType);
	}

	void ListModel::showDeleteConfirmationMessageBox(genericUI::MessageBox::Callback _callback)
	{
		genericUI::MessageBox::showYesNo(juce::MessageBoxIconType::WarningIcon, "Confirmation needed", "Delete selected patches from bank?", std::move(_callback));
	}

	void ListModel::updateEntries() const
	{
		const auto& patches = getPatches();

		auto& list = m_list->getList();

		// remove entries if we have too many
		while (list.size() > patches.size())
			list.removeEntry(list.size() - 1);

		// either update existing entries or create new ones
		for (size_t i = 0; i < patches.size(); ++i)
		{
			if (i < list.size())
			{
				auto* entry = dynamic_cast<ListItem*>(list.getEntry(i).get());
				assert(entry);
				if (entry)
					entry->setPatch(patches[i]);
			}
			else
			{
				auto entry = std::make_shared<ListItem>(m_patchManager, list);
				entry->setPatch(patches[i]);
				list.addEntry(std::move(entry));
			}
		}
	}

	void ListModel::setFilter(const std::string& _filter)
	{
		setFilter(_filter, m_hideDuplicatesByHash, m_hideDuplicatesByName);
	}

	void ListModel::setFilter(const std::string& _filter, const bool _hideDuplicatesByHash, const bool _hideDuplicatesByName)
	{
		if (m_filter == _filter && _hideDuplicatesByHash == m_hideDuplicatesByHash && m_hideDuplicatesByName == _hideDuplicatesByName)
			return;

		const auto selected = getSelectedPatches();

		m_filter = _filter;
		m_hideDuplicatesByHash = _hideDuplicatesByHash;
		m_hideDuplicatesByName = _hideDuplicatesByName;

		filterPatches();

		updateEntries();

		setSelectedPatches(selected);

		getPatchManager().setListStatus(static_cast<uint32_t>(selected.size()), static_cast<uint32_t>(getPatches().size()));
	}

	void ListModel::onSelectionChanged() const
	{
		if (m_activateOnSelectionChange)
			activateSelectedPatch();

		const auto patches = getSelectedPatches();

		getPatchManager().setListStatus(static_cast<uint32_t>(patches.size()), static_cast<uint32_t>(getPatches().size()));
	}
}
