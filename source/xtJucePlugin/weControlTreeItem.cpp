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
		if(const auto wave = m_editor.getData().getWave(m_wave))
			WaveTreeItem::paintWave(*wave, _g, _width>>1, 0, _width>>1, _height, juce::Colour(0xffffffff));

		TreeItem::paintItem(_g, _width, _height);
	}

	void ControlTreeItem::setWave(xt::WaveId _wave)
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

		auto table = m_editor.getData().getTable(m_table);

		if (!table)
			return;

		if (xt::State::isSpeech(*table))
		{
			setText("Speech");
			if (m_index.rawId() == 2)
				setWave(m_editor.getData().getWaveId(_table, m_index));
			else
				m_wave.invalidate();
		}
		else if (xt::State::isUpaw(*table))
		{
			setText("UPAW");
			m_wave.invalidate();
		}
		else
		{
			switch (m_index.rawId())
			{
			case 61: setWave(xt::WaveId(101)); break;
			case 62: setWave(xt::WaveId(100)); break;
			case 63: setWave(xt::WaveId(104)); break;
			default: setWave(m_editor.getData().getWaveId(_table, m_index)); break;
			}
		}
	}

	juce::var ControlTreeItem::getDragSourceDescription()
	{
		if(m_wave == g_invalidWaveIndex || xt::wave::isReadOnly(m_table) || xt::wave::isReadOnly(m_index))
			return TreeViewItem::getDragSourceDescription();

		auto* desc = new WaveDesc(m_editor);

		desc->waveIds = {m_wave};
		desc->source = WaveDescSource::ControlTableList;
		desc->tableIndex = m_index;

		desc->fillData(m_editor.getData());

		return desc;
	}

	bool ControlTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		if(xt::wave::isReadOnly(m_table) || xt::wave::isReadOnly(m_index))
			return false;
		auto* desc = WaveDesc::fromDragSource(_dragSourceDetails);
		if (desc == nullptr)
			return false;
		if (desc->source == WaveDescSource::WaveList && desc->waveIds.size() != 1)
			return false;
		if (desc->source == WaveDescSource::ControlTableList && desc->tableIndex == m_index)
			return false;
		return true;
	}

	void ControlTreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails, int _insertIndex)
	{
		const auto* waveDesc = WaveDesc::fromDragSource(_dragSourceDetails);
		if(!waveDesc)
			return;

		auto& data = m_editor.getData();

		// if the source is the control list, we swap two entries. if the source is the wave list, we add a new wave
		if(waveDesc->source == WaveDescSource::ControlTableList)
		{
			if(data.swapTableEntries(m_table, m_index, waveDesc->tableIndex))
			{
				setSelected(true, true, juce::dontSendNotification);
				data.sendTableToDevice(m_table);
			}
		}
		else if(waveDesc->source == WaveDescSource::WaveList && waveDesc->waveIds.size() == 1)
		{
			if(data.setTableWave(m_table, m_index, waveDesc->waveIds.front()))
				data.sendTableToDevice(m_table);
		}
	}

	void ControlTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!_mouseEvent.mods.isPopupMenu())
		{
			TreeItem::itemClicked(_mouseEvent);
			return;
		}

		juce::PopupMenu menu;

		menu.addItem("Remove", [this]
		{
			if (m_editor.getData().setTableWave(m_table, m_index, g_invalidWaveIndex))
				m_editor.getData().sendTableToDevice(m_table);
		});
		menu.addItem("Select Wave", [this]
		{
			m_editor.setSelectedWave(m_wave);
		});
		menu.addSubMenu("Copy to", m_editor.createCopyToSelectedTableMenu(m_wave));

		menu.showMenuAsync({});
	}

	void ControlTreeItem::onWaveChanged() const
	{
		repaintItem();
	}
}
