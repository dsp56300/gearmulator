#include "weControlTree.h"

#include "weControlTreeItem.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	ControlTree::ControlTree(WaveEditor& _editor) : Tree(_editor)
	{
		m_onTableChanged.set(_editor.getData().onTableChanged, [this](const uint32_t& _index)
		{
			if(_index == m_table)
				onTableChanged(true);
		});

		for(uint32_t i=0; i<m_items.size(); ++i)
		{
			m_items[i] = new ControlTreeItem(_editor, i);
			getRootItem()->addSubItem(m_items[i]);
		}
		setIndentSize(5);
	}

	void ControlTree::setTable(const uint32_t _index)
	{
		if(_index == m_table)
			return;
		m_table = _index;
		onTableChanged(false);
	}

	void ControlTree::onTableChanged(bool _tableHasChanged)
	{
		for (auto* item : m_items)
			item->setTable(m_table, _tableHasChanged);
	}
}
