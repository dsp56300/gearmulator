#include "patchmanager.h"

#include "datasourcetreeitem.h"
#include "info.h"
#include "list.h"
#include "searchlist.h"
#include "searchtree.h"
#include "tree.h"

#include "../pluginEditor.h"

namespace jucePluginEditorLib::patchManager
{
	constexpr int g_scale = 2;
	constexpr auto g_searchBarHeight = 32;
	constexpr int g_padding = 4;

	PatchManager::PatchManager(genericUI::Editor& _editor, Component* _root, const juce::File& _dir) : DB(_dir), m_editor(_editor), m_state(*this)
	{
		const auto rootW = _root->getWidth() / g_scale;
		const auto rootH = _root->getHeight() / g_scale;
		const auto scale = juce::AffineTransform::scale(g_scale);

		setSize(rootW, rootH);
		setTransform(scale);

		_root->addAndMakeVisible(this);

		m_tree = new Tree(*this);
		m_tree->setSize(rootW / 3 - g_padding, rootH - g_searchBarHeight - g_padding);

		m_searchTree = new SearchTree(*m_tree);
		m_searchTree->setSize(m_tree->getWidth(), g_searchBarHeight);
		m_searchTree->setTopLeftPosition(m_tree->getX(), m_tree->getHeight() + g_padding);

		addAndMakeVisible(m_tree);
		addAndMakeVisible(m_searchTree);

		m_list = new List(*this);
		m_list->setSize(rootW / 3 - g_padding, rootH - g_searchBarHeight - g_padding);
		m_list->setTopLeftPosition(m_tree->getWidth() + g_padding, 0);

		m_searchList = new SearchList(*m_list);
		m_searchList->setSize(m_list->getWidth(), g_searchBarHeight);
		m_searchList->setTopLeftPosition(m_list->getX(), m_list->getHeight() + g_padding);

		addAndMakeVisible(m_list);
		addAndMakeVisible(m_searchList);

		m_info = new Info(*this);
		m_info->setTopLeftPosition(m_tree->getWidth() + m_list->getWidth() + g_padding * 2, 0);
		m_info->setSize(getWidth() - m_info->getX(), rootH);

		addAndMakeVisible(m_info);

		if(const auto t = getTemplate("pm_search"))
		{
			t->apply(getEditor(), *m_searchList);
			t->apply(getEditor(), *m_searchTree);
		}

		startTimer(200);
	}

	PatchManager::~PatchManager()
	{
		stopTimer();
		delete m_searchTree;
		delete m_searchList;
		delete m_tree;
		delete m_list;
		delete m_info;
	}

	void PatchManager::timerCallback()
	{
		pluginLib::patchDB::Dirty dirty;
		uiProcess(dirty);

		m_tree->processDirty(dirty);
		m_list->processDirty(dirty);
	}

	void PatchManager::setSelectedSearch(const pluginLib::patchDB::SearchHandle& _handle) const
	{
		m_list->setContent(_handle);
	}

	void PatchManager::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch, uint32_t _indexInSearch)
	{
		m_info->setPatch(_patch);

		m_state.setSelectedPatch(getCurrentPart(), _patch, _fromSearch, _indexInSearch);

		activatePatch(_patch);
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch)
	{
		if(!isValid(_patch))
			return false;

		// assume that we already know the patch if the state is valid and the name is a match
		if(m_state.isValid(_part))
		{
			const auto knownPatch = m_state.getPatch(_part);

			if(knownPatch && knownPatch->name == _patch->name)
				return true;
		}

		const auto& currentPatch = _patch;

		// we've got a patch, but we do not know its search handle, i.e. which list it is part of, find the missing information

		const auto patchDs = _patch->source.lock();

		if(!patchDs)
			return false;

		pluginLib::patchDB::SearchHandle searchHandle = pluginLib::patchDB::g_invalidSearchHandle;

		if(auto* item = m_tree->getItem(*patchDs))
		{
			searchHandle = item->getSearchHandle();

			if(getCurrentPart() == _part)
				item->setSelected(true, true);
		}

		if(searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
		{
			const auto search = getSearch(*patchDs);

			if(!search)
				return false;
			searchHandle = search->handle;
		}

		const auto [patches, index] = m_state.getPatchesAndIndex(currentPatch, searchHandle);

		if(index == pluginLib::patchDB::g_invalidProgram)
			return false;

		m_state.setSelectedPatch(_part, currentPatch, searchHandle, index);

		if(getCurrentPart() == _part)
		{
			if(!m_list->setSelectedPatches({currentPatch}))
				int foo=0;
		}

		return true;
	}

	bool PatchManager::selectPrevPreset(const uint32_t _part)
	{
		return selectPreset(_part, -1);
	}

	bool PatchManager::selectNextPreset(const uint32_t _part)
	{
		return selectPreset(_part, 1);
	}

	std::shared_ptr<genericUI::UiObject> PatchManager::getTemplate(const std::string& _name) const
	{
		return m_editor.getTemplate(_name);
	}

	void PatchManager::onLoadFinished()
	{
		DB::onLoadFinished();

		for(uint32_t i=0; i<16; ++i)
			updateStateAsync(i);
	}

	void PatchManager::updateStateAsync(uint32_t _part)
	{
		pluginLib::patchDB::Data data;
		if(!requestPatchForPart(data, _part))
			return;
		const auto patch = initializePatch(data);
		if(!patch)
			return;
		updateStateAsync(_part, patch);
	}

	pluginLib::patchDB::SearchHandle PatchManager::updateStateAsync(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch)
	{
		if(!isValid(_patch))
			return pluginLib::patchDB::g_invalidSearchHandle;

		// assume that we already know the patch if the state is valid and the name is a match
		if(m_state.isValid(_part))
		{
			const auto knownPatch = m_state.getPatch(_part);

			if(knownPatch && knownPatch->name == _patch->name)
				return pluginLib::patchDB::g_invalidSearchHandle;
		}

		const auto patchDs = _patch->source.lock();

		if(patchDs)
		{
			setSelectedPatch(_part, _patch);
			return pluginLib::patchDB::g_invalidSearchHandle;
		}

		// we've got a patch, but we do not know its datasource and search handle, find the data source by executing a search

		const auto searchHandle = findDatasourceForPatch(_patch, [this, _part](const pluginLib::patchDB::Search& _search)
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

			runOnUiThread([this, _part, currentPatch, handle]
			{
				cancelSearch(handle);
				updateStateAsync(_part, currentPatch);
			});
		});

		return searchHandle;
	}

	bool PatchManager::selectPreset(const uint32_t _part, const int _offset)
	{
		auto [patch, index] = m_state.getNeighbourPreset(_part, _offset);

		if(!patch)
		{
			// if the current patch is unknown, request the current patch and find the source of it
			const auto currentPatch = requestPatchForPart(_part);
			if(!currentPatch)
				return false;

			if(!setSelectedPatch(_part, currentPatch))
				return false;

			const auto searchHandle = m_state.getSearchHandle(_part);

			const auto& [p, i] = m_state.getNeighbourPreset(currentPatch, searchHandle, _offset);

			if(!p)
				return false;

			patch = p;
			index = i;
		}

		if(!activatePatch(patch, _part))
			return false;

		m_state.setSelectedPatch(_part, patch, m_state.getSearchHandle(_part), index);

		if(getCurrentPart() == _part)
			m_list->setSelectedPatches({patch});

		return true;
	}
}
