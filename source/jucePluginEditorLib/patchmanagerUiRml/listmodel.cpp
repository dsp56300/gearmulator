#include "listmodel.h"

#include "listitem.h"
#include "patchmanagerUiRml.h"

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#include "jucePluginEditorLib/patchmanager/patchmanager.h"

#include "jucePluginLib/filetype.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlElemList.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInplaceEditor.h"
#include "juceRmlUi/rmlMenu.h"

#include "juceUiLib/messageBox.h"

namespace jucePluginEditorLib::patchManagerRml
{
	namespace
	{
		Rml::ElementInstancerGeneric<ListElemEntry> g_instancer;

		constexpr float g_selectionChangeRateLimit = 0.25f;
	}

	ListModel::ListModel(PatchManagerUiRml& _pm, juceRmlUi::ElemList* _list) : m_patchManager(_pm), m_list(_list)
	{
		const auto& config = m_patchManager.getEditor().getProcessor().getConfig();

		m_hideDuplicatesByHash = config.getIntValue("pm_hideDuplicatesByHash", 0) != 0;
		m_hideDuplicatesByName = config.getIntValue("pm_hideDuplicatesByName", 0) != 0;

		_list->setInstancer(&g_instancer);
		_list->SetAttribute("model", this);

		auto& list = _list->getList();

		list.setMultiselect(true);
		list.evSelectionChanged.addListener([this](juceRmlUi::List* const&) { onSelectionChanged(); });

		juceRmlUi::EventListener::Add(_list, Rml::EventId::Keydown, [this](Rml::Event& _event)
		{
			onListKeyDown(_event);
		});
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

	std::set<ListModel::PatchKey> ListModel::getSelectedPatchKeys() const
	{
		auto patches = getSelectedPatches();
		std::set<pluginLib::patchDB::PatchKey> result;
		for (const auto& patch : patches)
			result.insert(pluginLib::patchDB::PatchKey(*patch));
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
		auto selectedPatches = getSelectedPatchKeys();

		m_search = _search;

		m_patches.clear();
		{
			std::shared_lock lock(_search->resultsMutex);
			m_patches.insert(m_patches.end(), _search->results.begin(), _search->results.end());
		}

		sortPatches();
		filterPatches();

		updateEntries();

		if (selectedPatches.empty())
		{
			// if there is no selection, always select the currently active patch for the current part
			selectedPatches.insert(getDB().getState().getPatch(getDB().getCurrentPart()));
		}

		if (setSelectedPatches(selectedPatches))
		{
			m_list->scrollIntoView(getSelectedEntries().front());
		}
		else
		{
			m_list->SetScrollTop(0);
			m_list->SetScrollLeft(0);
		}

		getPatchManager().setListStatus(static_cast<uint32_t>(getSelectedEntries().size()), static_cast<uint32_t>(getPatches().size()));
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

	pluginLib::patchDB::PatchPtr ListModel::getPatch(const size_t _index) const
	{
		const auto& patches = getPatches();
		if (_index >= patches.size())
			return {};
		return patches[_index];
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
		const auto t = Search::lowercase(name);
		return t.find(m_filter) != std::string::npos;
	}

	pluginLib::patchDB::DataSourceNodePtr ListModel::getDatasource() const
	{
		if (!m_search)
			return {};
		if (m_search->request.sourceNode)
			return m_search->request.sourceNode;
		return {};
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

	void ListModel::activateSelectedPatch()
	{
		const auto patches = getSelectedPatches();

		if(patches.size() != 1)
			return;

		const auto now = static_cast<float>(m_list->GetCoreInstance().system_interface->GetElapsedTime());
		const auto elapsed = now - m_lastActivationTime;

		if(elapsed >= g_selectionChangeRateLimit)
		{
			getDB().setSelectedPatch(*patches.begin(), m_search->handle);
			m_lastActivationTime = now;
		}
		else
		{
			m_activatePatch = *patches.begin();
			m_activateSearchHandle = m_search->handle;

			m_activatePatchDelay.reset(new juceRmlUi::DelayedCall(m_list, g_selectionChangeRateLimit - elapsed, [this]
			{
				if(!m_activatePatch)
					return;

				getDB().setSelectedPatch(m_activatePatch, m_activateSearchHandle);
				m_lastActivationTime = static_cast<float>(m_list->GetCoreInstance().system_interface->GetElapsedTime());
				m_activatePatch = {};
				m_activateSearchHandle = pluginLib::patchDB::g_invalidSearchHandle;

				juceRmlUi::RmlComponent::fromElement(m_list)->enqueueUpdate();
			}, false));
		}
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
			bool haveSeparator = false;

			auto tryAddSeparator = [&]()
			{
				if (!haveSeparator)
					menu.addSeparator();
				haveSeparator = true;
			};

			pluginLib::patchDB::TypedTags tags;

			for (const auto& selectedPatch : selectedPatches)
				tags.add(selectedPatch->getTags());

			if(selectedPatches.size() == 1)
			{
				tryAddSeparator();

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
							setContent(m_search);
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
				tryAddSeparator();

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
				tryAddSeparator();

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

	bool ListModel::hasFilters() const
	{
		return !m_filter.empty() || m_hideDuplicatesByHash || m_hideDuplicatesByName || !m_search->request.tags.empty();
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
		genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Warning, "Confirmation needed", "Delete selected patches from bank?", std::move(_callback));
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

		auto& config = m_patchManager.getEditor().getProcessor().getConfig();

		if (m_hideDuplicatesByHash != _hideDuplicatesByHash)
		{
			m_hideDuplicatesByHash = _hideDuplicatesByHash;
			config.setValue("pm_hideDuplicatesByHash", m_hideDuplicatesByHash);
		}
		if (m_hideDuplicatesByName != _hideDuplicatesByName)
		{
			m_hideDuplicatesByName = _hideDuplicatesByName;
			config.setValue("pm_hideDuplicatesByName", m_hideDuplicatesByName);
		}

		filterPatches();

		updateEntries();

		setSelectedPatches(selected);

		getPatchManager().setListStatus(static_cast<uint32_t>(selected.size()), static_cast<uint32_t>(getPatches().size()));
	}

	void ListModel::onSelectionChanged()
	{
		if (m_activateOnSelectionChange)
			activateSelectedPatch();

		const auto patches = getSelectedPatches();

		getPatchManager().setListStatus(static_cast<uint32_t>(patches.size()), static_cast<uint32_t>(getPatches().size()));
	}

	void ListModel::onListKeyDown(Rml::Event& _event)
	{
		const auto key = juceRmlUi::helper::getKeyIdentifier(_event);

		if (key != Rml::Input::KeyIdentifier::KI_F2)
			return;
		if (getSelectedEntries().size() != 1)
			return;

		const auto& listEntry = m_list->getList().getEntry(getSelectedEntries().front());

		const auto elem = listEntry->getElement();

		if (!elem)
			return;

		const auto patch = getPatch(getSelectedEntries().front());

		new juceRmlUi::InplaceEditor(elem, patch->getName(), [this, patch](const std::string& _newName)
		{
			getDB().renamePatch(patch, _newName);
			setContent(m_search);
		});

		_event.StopPropagation();
	}
}
