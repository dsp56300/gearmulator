#include "patchmanager.h"

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
	}

	std::shared_ptr<genericUI::UiObject> PatchManager::getTemplate(const std::string& _name) const
	{
		return m_editor.getTemplate(_name);
	}
}
