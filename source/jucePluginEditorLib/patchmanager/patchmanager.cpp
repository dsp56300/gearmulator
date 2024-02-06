#include "patchmanager.h"

#include "datasourcetree.h"
#include "datasourcetreeitem.h"
#include "info.h"
#include "list.h"
#include "searchlist.h"
#include "searchtree.h"
#include "status.h"
#include "tagstree.h"
#include "tree.h"

#include "../pluginEditor.h"

#include "../../jucePluginLib/types.h"
#include "juce_gui_extra/misc/juce_ColourSelector.h"

namespace jucePluginEditorLib::patchManager
{
	constexpr int g_scale = 2;
	constexpr auto g_searchBarHeight = 32;
	constexpr int g_padding = 4;

	PatchManager::PatchManager(Editor& _editor, Component* _root, const juce::File& _dir) : DB(_dir), m_editor(_editor), m_state(*this)
	{
		const auto rootW = _root->getWidth() / g_scale;
		const auto rootH = _root->getHeight() / g_scale;
		const auto scale = juce::AffineTransform::scale(g_scale);

		setSize(rootW, rootH);
		setTransform(scale);

		_root->addAndMakeVisible(this);

		// 1st column
		auto w = rootW / 3;
		m_treeDS = new DatasourceTree(*this);
		m_treeDS->setSize(w - g_padding, rootH - g_searchBarHeight - g_padding);

		m_searchTreeDS = new SearchTree(*m_treeDS);
		m_searchTreeDS->setSize(m_treeDS->getWidth(), g_searchBarHeight);
		m_searchTreeDS->setTopLeftPosition(m_treeDS->getX(), m_treeDS->getHeight() + g_padding);

		addAndMakeVisible(m_treeDS);
		addAndMakeVisible(m_searchTreeDS);

		// 2nd column
		w >>= 1;
		m_treeTags = new TagsTree(*this);
		m_treeTags->setTopLeftPosition(m_treeDS->getRight() + g_padding, 0);
		m_treeTags->setSize(w - g_padding, rootH - g_searchBarHeight - g_padding);

		m_searchTreeTags = new SearchTree(*m_treeTags);
		m_searchTreeTags->setTopLeftPosition(m_treeTags->getX(), m_treeTags->getHeight() + g_padding);
		m_searchTreeTags->setSize(m_treeTags->getWidth(), g_searchBarHeight);

		addAndMakeVisible(m_treeTags);
		addAndMakeVisible(m_searchTreeTags);

		// 3rd column
		m_list = new List(*this);
		m_list->setTopLeftPosition(m_treeTags->getRight() + g_padding, 0);
		m_list->setSize(w - g_padding, rootH - g_searchBarHeight - g_padding);

		m_searchList = new SearchList(*m_list);
		m_searchList->setTopLeftPosition(m_list->getX(), m_list->getHeight() + g_padding);
		m_searchList->setSize(m_list->getWidth(), g_searchBarHeight);

		addAndMakeVisible(m_list);
		addAndMakeVisible(m_searchList);

		// 4th column
		m_info = new Info(*this);
		m_info->setTopLeftPosition(m_list->getRight() + g_padding, 0);
		m_info->setSize(getWidth() - m_info->getX(), rootH - g_searchBarHeight - g_padding);

		m_status = new Status();
		m_status->setTopLeftPosition(m_info->getX(), m_info->getHeight() + g_padding);
		m_status->setSize(m_info->getWidth(), g_searchBarHeight);

		addAndMakeVisible(m_info);
		addAndMakeVisible(m_status);

		if(const auto t = getTemplate("pm_search"))
		{
			t->apply(getEditor(), *m_searchList);
			t->apply(getEditor(), *m_searchTreeDS);
			t->apply(getEditor(), *m_searchTreeTags);
		}

		if(const auto t = getTemplate("pm_status_label"))
		{
			t->apply(getEditor(), *m_status);
		}

		startTimer(200);
	}

	PatchManager::~PatchManager()
	{
		stopTimer();

		delete m_status;
		delete m_info;
		delete m_searchList;
		delete m_list;

		// trees emit onSelectionChanged, be sure to guard it 
		m_list = nullptr;

		delete m_searchTreeTags;
		delete m_treeTags;
		delete m_searchTreeDS;
		delete m_treeDS;
	}

	void PatchManager::timerCallback()
	{
		pluginLib::patchDB::Dirty dirty;
		uiProcess(dirty);

		m_treeDS->processDirty(dirty);
		m_treeTags->processDirty(dirty);
		m_list->processDirty(dirty);

		m_status->setScanning(isScanning());

		if(!dirty.errors.empty())
		{
			std::string msg = "Patch Manager encountered errors:\n\n";
			for(size_t i=0; i<dirty.errors.size(); ++i)
			{
				msg += dirty.errors[i];
				if(i < dirty.errors.size() - 1)
					msg += "\n";
			}

			juce::NativeMessageBox::showMessageBox(juce::AlertWindow::WarningIcon, "Patch Manager Error", msg);
		}
	}

	void PatchManager::setSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		m_selectedItems[_tree] = std::set{_item};

		if(_tree == m_treeDS)
			m_treeTags->onParentSearchChanged(_item->getSearchRequest());

		onSelectedItemsChanged();
	}

	void PatchManager::addSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		const auto oldCount = m_selectedItems[_tree].size();
		m_selectedItems[_tree].insert(_item);
		const auto newCount = m_selectedItems[_tree].size();
		if(newCount > oldCount)
			onSelectedItemsChanged();
	}

	void PatchManager::removeSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		const auto it = m_selectedItems.find(_tree);
		if(it == m_selectedItems.end())
			return;
		if(!it->second.erase(_item))
			return;
		onSelectedItemsChanged();
	}

	bool PatchManager::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::patchDB::SearchHandle _fromSearch)
	{
		return setSelectedPatch(getCurrentPart(), _patch, _fromSearch);
	}
	
	bool PatchManager::selectPatch(const uint32_t _part, const int _offset)
	{
		auto [patch, _] = m_state.getNeighbourPreset(_part, _offset);

		if(!patch)
			return false;

		if(!setSelectedPatch(_part, patch, m_state.getSearchHandle(_part)))
			return false;

		if(_part == getCurrentPart())
			m_list->setSelectedPatches({patch});

		return true;
	}

	bool PatchManager::setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch)
	{
		if(!activatePatch(_patch, _part))
			return false;

		m_state.setSelectedPatch(getCurrentPart(), pluginLib::patchDB::PatchKey(*_patch), _fromSearch);

		if(_part == getCurrentPart())
			m_info->setPatch(_patch);

		return true;
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch)
	{
		if(!isValid(_patch))
			return false;

		const auto patchDs = _patch->source.lock();

		if(!patchDs)
			return false;

		if(!setSelectedPatch(_part, pluginLib::patchDB::PatchKey(*_patch)))
			return false;

		return true;
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchKey& _patch)
	{
		// we've got a patch, but we do not know its search handle, i.e. which list it is part of, find the missing information

		if(!_patch.isValid())
			return false;

		const auto searchHandle = getSearchHandle(*_patch.source, _part == getCurrentPart());

		if(searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return false;

		m_state.setSelectedPatch(_part, _patch, searchHandle);

		if(getCurrentPart() == _part)
			m_list->setSelectedPatches({_patch});

		return true;
	}

	bool PatchManager::selectPrevPreset(const uint32_t _part)
	{
		return selectPatch(_part, -1);
	}

	bool PatchManager::selectNextPreset(const uint32_t _part)
	{
		return selectPatch(_part, 1);
	}

	bool PatchManager::selectPatch(const uint32_t _part, const pluginLib::patchDB::DataSource& _ds, const uint32_t _program)
	{
		const auto searchHandle = getSearchHandle(_ds, _part == getCurrentPart());

		if(searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return false;

		auto s = getSearch(searchHandle);
		if(!s)
			return false;

		pluginLib::patchDB::PatchPtr p;

		std::shared_lock lockResults(s->resultsMutex);
		for (const auto& patch : s->results)
		{
			if(patch->program == _program)
			{
				p = patch;
				break;
			}
		}

		if(!p)
			return false;

		if(!activatePatch(p, _part))
			return false;

		setSelectedPatch(_part, p, s->handle);

		if(_part == getCurrentPart())
			m_list->setSelectedPatches({p});

		return true;
	}

	void PatchManager::setListStatus(uint32_t _selected, uint32_t _total)
	{
		m_status->setListStatus(_selected, _total);
	}

	pluginLib::patchDB::Color PatchManager::getPatchColor(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		// we want to prevent that a whole list is colored with one color just because that list is based on a tag, prefer other tags instead
		pluginLib::patchDB::TypedTags ignoreTags;

		for (const auto& selectedItem : m_selectedItems)
		{
			for (const auto& item : selectedItem.second)
			{
				const auto& s = item->getSearchRequest();
				ignoreTags.add(s.tags);
			}
		}
		return DB::getPatchColor(_patch, ignoreTags);
	}

	bool PatchManager::addGroupTreeItemForTag(const pluginLib::patchDB::TagType _type, const std::string& _name)
	{
		const auto groupType = toGroupType(_type);
		if(groupType == GroupType::Invalid)
			return false;
		if(_name.empty())
			return false;
		if(m_treeTags->getItem(groupType))
			return false;
		m_treeTags->addGroup(groupType, _name);
		return true;
	}

	void PatchManager::paint(juce::Graphics& g)
	{
		g.fillAll(juce::Colour(0,0,0));
	}

	void PatchManager::exportPresets(const juce::File& _file, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, FileType _fileType) const
	{
		FileType type = _fileType;
		const auto name = Editor::createValidFilename(type, _file);

		std::vector<pluginLib::patchDB::Data> patchData;
		for (const auto& patch : _patches)
		{
			const auto patchSysex = prepareSave(patch);

			if(!patchSysex.empty())
				patchData.push_back(patchSysex);
		}

		if(!getEditor().savePresets(type, name, patchData))
			juce::NativeMessageBox::showMessageBox(juce::AlertWindow::WarningIcon, "Save failed", "Failed to write data to " + _file.getFullPathName().toStdString());
	}

	bool PatchManager::exportPresets(std::vector<pluginLib::patchDB::PatchPtr>&& _patches, FileType _fileType) const
	{
		if(_patches.size() > 128)
		{
			if(1 != juce::NativeMessageBox::showOkCancelBox(juce::AlertWindow::WarningIcon, 
				"Patch Manager",
				"You are trying to export more than 128 presets into a single file. Note that this dump exceeds the size of one bank and may not be compatible with your hardware"))
				return true;
		}

		List::sortPatches(_patches, pluginLib::patchDB::SourceType::LocalStorage);

		getEditor().savePreset([this, p = std::move(_patches), _fileType](const juce::File& _file)
		{
			exportPresets(_file, p, _fileType);
		});

		return true;
	}

	std::shared_ptr<genericUI::UiObject> PatchManager::getTemplate(const std::string& _name) const
	{
		return m_editor.getTemplate(_name);
	}

	void PatchManager::onLoadFinished()
	{
		DB::onLoadFinished();

		for(uint32_t i=0; i<m_state.getPartCount(); ++i)
		{
			const auto p = m_state.getPatch(i);

			// If the state has been deserialized, the patch key is valid but the search handle is not. Only restore if that is the case
			if(p.isValid() && m_state.getSearchHandle(i) == pluginLib::patchDB::g_invalidSearchHandle)
			{
				if(!setSelectedPatch(i, p))
					m_state.clear(i);
			}
			else if(!m_state.isValid(i))
			{
				// otherwise, try to restore from the currently loaded patch
				updateStateAsync(i, requestPatchForPart(i));
			}
		}
	}

	void PatchManager::setPerInstanceConfig(const std::vector<uint8_t>& _data)
	{
		if(_data.empty())
			return;
		try
		{
			pluginLib::PluginStream s(_data);
			const auto version = s.read<uint32_t>();
			if(version != 1)
				return;
			m_state.setConfig(s);
		}
		catch(std::range_error&)
		{
		}
	}

	void PatchManager::getPerInstanceConfig(std::vector<uint8_t>& _data)
	{
		pluginLib::PluginStream s;
		s.write<uint32_t>(1);	// version
		m_state.getConfig(s);
		s.toVector(_data);
	}

	void PatchManager::onProgramChanged(const uint32_t _part)
	{
		if(isLoading())
			return;
		return;
		pluginLib::patchDB::Data data;
		if(!requestPatchForPart(data, _part))
			return;
		const auto patch = initializePatch(std::move(data));
		if(!patch)
			return;
		updateStateAsync(_part, patch);
	}

	void PatchManager::setCurrentPart(uint32_t _part)
	{
		if(!m_state.isValid(_part))
			return;

		setSelectedPatch(_part, m_state.getPatch(_part));
	}

	void PatchManager::updateStateAsync(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch)
	{
		if(!isValid(_patch))
			return;

		const auto patchDs = _patch->source.lock();

		if(patchDs)
		{
			setSelectedPatch(_part, _patch);
			return;
		}

		// we've got a patch, but we do not know its datasource and search handle, find the data source by executing a search

		findDatasourceForPatch(_patch, [this, _part](const pluginLib::patchDB::Search& _search)
		{
			const auto handle = _search.handle;

			std::vector<pluginLib::patchDB::PatchPtr> results;
			results.assign(_search.results.begin(), _search.results.end());

			if(results.empty())
				return;

			if(results.size() > 1)
			{
				// if there are multiple results, sort them, we prefer ROM results over other results

				std::sort(results.begin(), results.end(), [](const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b)
				{
					const auto dsA = _a->source.lock();
					const auto dsB = _b->source.lock();

					if(!dsA || !dsB)
						return true;

					if(dsA->type < dsB->type)
						return true;
					if(dsA->type > dsB->type)
						return false;
					if(dsA->name < dsB->name)
						return true;
					if(dsA->name > dsB->name)
						return false;
					if(_a->program < _b->program)
						return true;
					return false;
				});
			}

			const auto currentPatch = results.front();

			const auto key = pluginLib::patchDB::PatchKey(*currentPatch);

			runOnUiThread([this, _part, key, handle]
			{
				cancelSearch(handle);
				setSelectedPatch(_part, key);
			});
		});
	}

	pluginLib::patchDB::SearchHandle PatchManager::getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem)
	{
		if(auto* item = m_treeDS->getItem(_ds))
		{
			const auto searchHandle = item->getSearchHandle();

			// select the tree item that contains the data source and expand all parents to make it visible
			if(_selectTreeItem)
			{
				item->setSelected(true, true);

				auto* parent = item->getParentItem();
				while(parent)
				{
					parent->setOpen(true);
					parent = parent->getParentItem();
				}
			}

			return searchHandle;
		}

		const auto search = getSearch(_ds);

		if(!search)
			return pluginLib::patchDB::g_invalidSearchHandle;

		return search->handle;
	}

	void PatchManager::onSelectedItemsChanged()
	{
		// trees emit onSelectionChanged in destructor, be sure to guard it 
		if(!m_list)
			return;

		const auto selectedTags = m_selectedItems[m_treeTags];

		auto selectItem = [&](const TreeItem* _item)
		{
			if(_item->getSearchHandle() != pluginLib::patchDB::g_invalidSearchHandle)
			{
				m_list->setContent(_item->getSearchHandle());
				return true;
			}
			return false;
		};

		if(!selectedTags.empty())
		{
			if(selectedTags.size() == 1)
			{
				if(selectItem(*selectedTags.begin()))
					return;
			}
			else
			{
				pluginLib::patchDB::SearchRequest search = (*selectedTags.begin())->getSearchRequest();
				for (const auto& selectedTag : selectedTags)
					search.tags.add(selectedTag->getSearchRequest().tags);
				m_list->setContent(std::move(search));
				return;
			}
		}

		const auto selectedDataSources = m_selectedItems[m_treeDS];

		if(!selectedDataSources.empty())
		{
			const auto* item = *selectedDataSources.begin();
			selectItem(item);
		}
	}

	void PatchManager::changeListenerCallback(juce::ChangeBroadcaster* _source)
	{
		auto* cs = dynamic_cast<juce::ColourSelector*>(_source);

		if(cs)
		{
			const auto tagType = static_cast<pluginLib::patchDB::TagType>(static_cast<int>(cs->getProperties()["tagType"]));
			const auto tag = cs->getProperties()["tag"].toString().toStdString();

			if(tagType != pluginLib::patchDB::TagType::Invalid && !tag.empty())
			{
				const auto color = cs->getCurrentColour();
				setTagColor(tagType, tag, color.getARGB());

				repaint();
			}
		}
	}
}
