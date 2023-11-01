#include "patchmanager.h"

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

		m_tree = new Tree();
		m_tree->setSize(rootW / 3, _root->getHeight() * g_scale);
		m_tree->setTransform(scale);

		_root->addAndMakeVisible(m_tree);

		startTimer(500);
	}

	PatchManager::~PatchManager()
	{
		stopTimer();
		delete m_tree;
	}

	void PatchManager::timerCallback()
	{
		Dirty dirty;
		uiProcess(dirty);
	}
}
