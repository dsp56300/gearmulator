#include "weWaveTree.h"

#include "weTypes.h"
#include "weWaveCategoryTreeItem.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	WaveTree::WaveTree(WaveEditor& _editor) : Tree(_editor)
	{
		addCategory(WaveCategory::Rom);
		addCategory(WaveCategory::User);
	}

	bool WaveTree::setSelectedWave(const xt::WaveId _id)
	{
		for (const auto& [category, item] : m_items)
		{
			if(item->setSelectedWave(_id))
				return true;
		}
		return false;
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
