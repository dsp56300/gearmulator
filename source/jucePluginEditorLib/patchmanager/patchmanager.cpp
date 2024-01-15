#include "patchmanager.h"

#include "datasourcetreeitem.h"
#include "info.h"
#include "list.h"
#include "searchlist.h"
#include "searchtree.h"
#include "tree.h"

#include "../pluginEditor.h"

#include "../../jucePluginLib/types.h"

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

	bool PatchManager::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::patchDB::SearchHandle _fromSearch)
	{
		return setSelectedPatch(getCurrentPart(), _patch, _fromSearch);
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

		pluginLib::patchDB::SearchHandle searchHandle = pluginLib::patchDB::g_invalidSearchHandle;

		if(auto* item = m_tree->getItem(*_patch.source))
		{
			searchHandle = item->getSearchHandle();

			// select the tree item that contains the data source and expand all parents to make it visible
			if(getCurrentPart() == _part)
			{
				item->setSelected(true, true);

				auto* parent = item->getParentItem();
				while(parent)
				{
					parent->setOpen(true);
					parent = parent->getParentItem();
				}
			}
		}

		if(searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
		{
			const auto search = getSearch(*_patch.source);

			if(!search)
				return false;

			searchHandle = search->handle;
		}

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

	bool PatchManager::selectPatch(const uint32_t _part, const int _offset)
	{
		auto [patch, _] = m_state.getNeighbourPreset(_part, _offset);

		if(!patch)
			return false;

		return setSelectedPatch(_part, patch, m_state.getSearchHandle(_part));
	}
}
