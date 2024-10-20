#include "weTablesTreeItem.h"

#include "weWaveDesc.h"
#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	TablesTreeItem::TablesTreeItem(WaveEditor& _editor, const xt::TableId _tableIndex) : m_editor(_editor), m_index(_tableIndex)
	{
		setPaintRootItemInBold(false);
		setDrawsInLeftMargin(true);

		const auto& wavetableNames = _editor.getEditor().getXtController().getParameterDescriptions().getValueList("waveType");

		const auto name = wavetableNames->valueToText(_tableIndex.rawId());

		setText(name);

		m_onTableChanged.set(_editor.getData().onTableChanged, [this](const xt::TableId& _index)
		{
			onTableChanged(_index);
		});
	}

	void TablesTreeItem::itemSelectionChanged(const bool _isNowSelected)
	{
		TreeItem::itemSelectionChanged(_isNowSelected);
		if(_isNowSelected)
			m_editor.setSelectedTable(m_index);
	}

	juce::var TablesTreeItem::getDragSourceDescription()
	{
		auto* waveDesc = new WaveDesc();
		waveDesc->tableId = m_index;
		waveDesc->source = WaveDescSource::TablesList;
		return waveDesc;
	}

	bool TablesTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
	{
		if(xt::wave::isReadOnly(m_index))
			return false;
		const auto* waveDesc = WaveDesc::fromDragSource(dragSourceDetails);
		if(!waveDesc || waveDesc->source != WaveDescSource::TablesList)
			return false;
		return waveDesc->tableId != m_index;
	}

	void TablesTreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex)
	{
		TreeItem::itemDropped(dragSourceDetails, insertIndex);

		const auto* waveDesc = WaveDesc::fromDragSource(dragSourceDetails);

		if(!waveDesc || waveDesc->source != WaveDescSource::TablesList || waveDesc->tableId == m_index)
			return;

		auto& data = m_editor.getData();
		if(data.copyTable(m_index, waveDesc->tableId))
		{
			setSelected(true, true, juce::dontSendNotification);
			m_editor.setSelectedTable(m_index);
			data.sendTableToDevice(m_index);
		}
	}

	void TablesTreeItem::onTableChanged(xt::TableId _index)
	{
		if(_index != m_index)
			return;
		onTableChanged();
	}

	void TablesTreeItem::onTableChanged()
	{
	}
}
