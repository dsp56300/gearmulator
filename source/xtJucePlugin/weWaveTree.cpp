#include "weWaveTree.h"

#include "weTypes.h"
#include "weWaveCategoryTreeItem.h"
#include "xtEditor.h"

namespace xtJucePlugin
{
	WaveTree::WaveTree(WaveEditor& _editor) : Tree(_editor)
	{
		addCategory(WaveCategory::Rom);
		addCategory(WaveCategory::User);
	}

	void WaveTree::addCategory(WaveCategory _category)
	{
		if(m_items.find(_category) != m_items.end())
			return;

		auto* item = new WaveCategoryTreeItem(getWaveEditor(), _category);
		getRootItem()->addSubItem(item);

		m_items.insert({_category, item});
	}
}
