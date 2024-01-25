#include "patchmanager.h"

#include "datasourcetree.h"
#include "datasourcetreeitem.h"
#include "info.h"
#include "list.h"
#include "searchlist.h"
#include "searchtree.h"
#include "tagstree.h"
#include "tree.h"

#include "../pluginEditor.h"

#include "../../jucePluginLib/types.h"

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
		m_info->setSize(getWidth() - m_info->getX(), rootH);

		addAndMakeVisible(m_info);

		if(const auto t = getTemplate("pm_search"))
		{
			t->apply(getEditor(), *m_searchList);
			t->apply(getEditor(), *m_searchTreeDS);
			t->apply(getEditor(), *m_searchTreeTags);
		}

		startTimer(200);
	}

	PatchManager::~PatchManager()
	{
		stopTimer();

		delete m_info;
		delete m_searchList;
		delete m_list;
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
	}

	void PatchManager::setSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		m_selectedItems[_tree] = std::set{_item};

		if(_tree == m_treeDS)
		{
			m_treeTags->onParentSearchChanged(_item->getSearchRequest());
		}

		onSelectedItemsChanged();
	}

	void PatchManager::addSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		m_selectedItems[_tree].insert(_item);
		onSelectedItemsChanged();
	}

	void PatchManager::removeSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		m_selectedItems[_tree].erase(_item);
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
		const auto patch = initializePatch(data);
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
			if(selectItem(*selectedTags.begin()))
				return;
		}

		const auto selectedDataSources = m_selectedItems[m_treeDS];

		if(!selectedDataSources.empty())
		{
			const auto* item = *selectedDataSources.begin();
			selectItem(item);
		}
	}
}
