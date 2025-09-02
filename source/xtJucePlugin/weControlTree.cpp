#include "weControlTree.h"

#include "weControlTreeItem.h"
#include "xtWaveEditor.h"
#include "RmlUi/Core/ElementInstancer.h"

namespace xtJucePlugin
{
	ControlTree::ControlTree(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor) : Tree(_coreInstance, _tag, _editor, true) 
	{
		SetClass("x-we-controltree", true);

		m_onTableChanged.set(_editor.getData().onTableChanged, [this](const xt::TableId& _index)
		{
			if(_index == m_table)
				onTableChanged(true);
		});

		for(uint16_t i=0; i<static_cast<uint16_t>(m_items.size()); ++i)
		{
			m_items[i] = getTree().getRoot()->createChild<ControlTreeNode>(static_cast<xt::TableIndex>(i));
		}
	}

	void ControlTree::setTable(const xt::TableId _index)
	{
		if(_index == m_table)
			return;
		m_table = _index;
		onTableChanged(false);
	}

	Rml::ElementPtr ControlTree::createChild(const std::string& _tag)
	{
		return Rml::ElementPtr(new ControlTreeItem(GetCoreInstance(), _tag, getWaveEditor()));
	}

	void ControlTree::onTableChanged(const bool _tableHasChanged) const
	{
		for (const auto& node : m_items)
		{
			auto* item = dynamic_cast<ControlTreeItem*>(node->getElement());
			if (item)
				item->setTable(m_table, _tableHasChanged);
		}
	}
}
