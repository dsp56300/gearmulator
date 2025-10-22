#include "weWaveTree.h"

#include "weTypes.h"
#include "weWaveCategoryTreeItem.h"
#include "weWaveTreeItem.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"
#include "jucePluginLib/patchdb/patchdbtypes.h"

namespace xtJucePlugin
{
	WaveTree::WaveTree(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor) : Tree(_coreInstance, _tag, _editor, true)
	{
		SetClass("x-we-wavetree", true);

		addCategory(WaveCategory::Rom);
		addCategory(WaveCategory::User);
	}

	Rml::ElementPtr WaveTree::createChild(const std::string& _tag)
	{
		// after creating the category item, the waves are populated
		auto type = m_nextChildType;
		m_nextChildType = NextChildType::Wave;

		if(type == NextChildType::Category)
			return Rml::ElementPtr(new WaveCategoryTreeItem(GetCoreInstance(), _tag, getWaveEditor()));

		return Rml::ElementPtr(new WaveTreeItem(GetCoreInstance(), _tag, getWaveEditor()));
	}

	bool WaveTree::setSelectedWave(const xt::WaveId _id)
	{
		for (const auto& [category, item] : m_items)
		{
			auto* elem = dynamic_cast<WaveCategoryTreeItem*>(item->getElement());
			if(elem->setSelectedWave(_id))
				return true;
		}
		return false;
	}

	void WaveTree::addCategory(WaveCategory _category)
	{
		if(m_items.find(_category) != m_items.end())
			return;

		m_nextChildType = NextChildType::Category;
		auto node = getTree().getRoot()->createChild<WaveCategoryNode>(_category);

		m_items.insert({_category, node});
	}
}
