#include "weTablesTreeItem.h"

#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	TablesTreeItem::TablesTreeItem(WaveEditor& _editor, const uint32_t _tableIndex) : m_editor(_editor), m_index(_tableIndex)
	{
		setPaintRootItemInBold(false);
		setDrawsInLeftMargin(true);

		const auto& wavetableNames = _editor.getEditor().getXtController().getParameterDescriptions().getValueList("waveType");

		const auto name = wavetableNames->valueToText(_tableIndex);

		setText(name);

		m_onTableChanged.set(_editor.getData().onTableChanged, [this](const uint32_t& _index)
		{
			onTableChanged(_index);
		});
	}

	void TablesTreeItem::itemSelectionChanged(bool _isNowSelected)
	{
		TreeItem::itemSelectionChanged(_isNowSelected);
		m_editor.setSelectedTable(m_index);
	}

	void TablesTreeItem::onTableChanged(uint32_t _index)
	{
		if(_index != m_index)
			return;
		onTableChanged();
	}

	void TablesTreeItem::onTableChanged()
	{
	}
}
