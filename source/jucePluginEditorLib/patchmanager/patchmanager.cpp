#include "patchmanager.h"

#include "info.h"
#include "list.h"
#include "searchlist.h"
#include "searchtree.h"
#include "tree.h"

namespace jucePluginEditorLib::patchManager
{
	constexpr int g_scale = 2;
	constexpr auto g_searchBarHeight = 32;

	PatchManager::PatchManager(Component* _root, const juce::File& _json) : DB(_json)
	{
		const auto rootW = _root->getWidth() / g_scale;
		const auto rootH = _root->getHeight() / g_scale;
		const auto scale = juce::AffineTransform::scale(g_scale);

		setSize(rootW, rootH);
		setTransform(scale);

		_root->addAndMakeVisible(this);

		m_tree = new Tree(*this);
		m_tree->setSize(rootW / 3, rootH - g_searchBarHeight);

		m_searchTree = new SearchTree(*m_tree);
		m_searchTree->setSize(rootW / 3, g_searchBarHeight);
		m_searchTree->setTopLeftPosition(0, m_tree->getHeight());

		addAndMakeVisible(m_tree);
		addAndMakeVisible(m_searchTree);

		m_list = new List(*this);
		m_list->setSize(rootW / 3, rootH - g_searchBarHeight);
		m_list->setTopLeftPosition(m_tree->getWidth(), 0);

		m_searchList = new SearchList(*m_list);
		m_searchList->setSize(rootW / 3, g_searchBarHeight);
		m_searchList->setTopLeftPosition(m_tree->getWidth(), m_list->getHeight());

		addAndMakeVisible(m_list);
		addAndMakeVisible(m_searchList);

		m_info = new Info(*this);
		m_info->setSize(rootW / 3, rootH);
		m_info->setTopLeftPosition(m_tree->getWidth() + m_list->getWidth(), 0);

		addAndMakeVisible(m_info);

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

	void PatchManager::setSelectedSearch(const pluginLib::patchDB::SearchHandle& _handle)
	{
		m_list->setContent(_handle);
	}

	void PatchManager::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
		m_info->setPatch(_patch);
	}
}
