#include "weControlTreeItem.h"

#include "weWaveDesc.h"
#include "weWaveTreeItem.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	class WaveEditor;

	ControlTreeItem::ControlTreeItem(WaveEditor& _editor, const xt::TableIndex _index) : m_editor(_editor), m_index(_index)
	{
		m_onWaveChanged.set(_editor.getData().onWaveChanged, [this](const xt::WaveId& _wave)
		{
			if(_wave == m_wave)
				onWaveChanged();
		});

		setPaintRootItemInBold(false);
		setDrawsInLeftMargin(true);

		// force initial update
		m_wave = xt::WaveId(0);
		setWave(xt::WaveId());
	}

	void ControlTreeItem::paintItem(juce::Graphics& _g, const int _width, const int _height)
	{
		if(const auto wave = m_editor.getData().getWave(m_table, m_index))
			WaveTreeItem::paintWave(*wave, _g, _width>>1, 0, _width>>1, _height, juce::Colour(0xffffffff));

		TreeItem::paintItem(_g, _width, _height);
	}

	void ControlTreeItem::setWave(const xt::WaveId _wave)
	{
		if(m_wave == _wave)
			return;
		m_wave = _wave;
		const auto name = WaveTreeItem::getWaveName(m_wave);
		char prefix[16] = {0};
		(void)snprintf(prefix, std::size(prefix), "%02d: ", m_index.rawId());
		setText(prefix + (name.empty() ? "-" : name));
		repaintItem();
	}

	void ControlTreeItem::setTable(const xt::TableId _table, const bool _tableHasChanged)
	{
		if(m_table == _table && !_tableHasChanged)
			return;
		m_table = _table;
		setWave(m_editor.getData().getWaveIndex(_table, m_index));
	}

	juce::var ControlTreeItem::getDragSourceDescription()
	{
		if(m_wave == g_invalidWaveIndex)
			return TreeViewItem::getDragSourceDescription();

		auto* desc = new WaveDesc();
		desc->waveId = m_wave;
		desc->source = WaveDescSource::ControlList;
		desc->tableIndex = m_index;
		return desc;
	}

	bool ControlTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		return WaveDesc::fromDragSource(_dragSourceDetails) != nullptr;
	}

	void ControlTreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails, int _insertIndex)
	{
		const auto* waveDesc = WaveDesc::fromDragSource(_dragSourceDetails);
		if(!waveDesc)
			return;

		auto& data = m_editor.getData();

		// if the source is the control list, we swap two entries. if the source is the wave list, we add a new wave
		if(waveDesc->source == WaveDescSource::ControlList)
		{
			data.swapTableEntries(m_table, m_index, waveDesc->tableIndex);
			setSelected(true, true, juce::dontSendNotification);
		}
		else if(waveDesc->source == WaveDescSource::WaveList)
		{
			data.setTableWave(m_table, m_index, waveDesc->waveId);
		}
	}

	void ControlTreeItem::onWaveChanged() const
	{
		repaintItem();
	}
}
