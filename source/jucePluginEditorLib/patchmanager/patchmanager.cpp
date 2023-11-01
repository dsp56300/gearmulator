#include "patchmanager.h"

#include "list.h"
#include "tree.h"
#include "treeitem.h"

namespace jucePluginEditorLib::patchManager
{
	constexpr int g_scale = 2;

	PatchManager::PatchManager(juce::Component* _root)
	{
		const auto rootW = _root->getWidth() / g_scale;
		const auto rootH = _root->getHeight() / g_scale;
		const auto scale = juce::AffineTransform::scale(g_scale);

		setSize(rootW, rootH);
		setTransform(scale);

		_root->addAndMakeVisible(this);

		m_tree = new Tree(*this);
		m_tree->setSize(rootW / 3, rootH);

		addAndMakeVisible(m_tree);

		m_list = new List(*this);
		m_list->setSize(rootW / 3, rootH);
		m_list->setTopLeftPosition(m_tree->getWidth(), 0);

		addAndMakeVisible(m_list);

		startTimer(500);
	}

	PatchManager::~PatchManager()
	{
		stopTimer();
		delete m_tree;
		delete m_list;
	}

	void PatchManager::timerCallback()
	{
		pluginLib::patchDB::Dirty dirty;
		uiProcess(dirty);

		m_tree->processDirty(dirty);
	}

	void PatchManager::setSelectedSearch(const pluginLib::patchDB::SearchHandle& _handle)
	{
		m_list->setContent(_handle);
	}
}
