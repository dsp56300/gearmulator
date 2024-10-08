#include "weControlTree.h"

#include "weControlTreeItem.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	ControlTree::ControlTree(WaveEditor& _editor) : Tree(_editor)
	{
		m_onTableChanged.set(_editor.getData().onTableChanged, [this](const xt::TableId& _index)
		{
			if(_index == m_table)
				onTableChanged(true);
		});

		for(uint16_t i=0; i<static_cast<uint16_t>(m_items.size()); ++i)
		{
			m_items[i] = new ControlTreeItem(_editor, static_cast<xt::TableIndex>(i));
			getRootItem()->addSubItem(m_items[i]);
		}
		setIndentSize(5);
	}

	void ControlTree::setTable(const xt::TableId _index)
	{
		if(_index == m_table)
			return;
		m_table = _index;
		onTableChanged(false);
	}

	void ControlTree::onTableChanged(const bool _tableHasChanged) const
	{
		for (auto* item : m_items)
			item->setTable(m_table, _tableHasChanged);
	}
}
