#include "weControlTreeItem.h"

#include "weWaveDesc.h"
#include "weWaveTreeItem.h"
#include "xtWaveEditor.h"

#include "juceRmlUi/rmlMenu.h"

namespace xtJucePlugin
{
	class WaveEditor;

	ControlTreeItem::ControlTreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor)
	: TreeItem(_coreInstance, _tag)
	, m_editor(_editor)
	{
		m_onWaveChanged.set(_editor.getData().onWaveChanged, [this](const xt::WaveId& _wave)
		{
			if(_wave == m_wave)
				onWaveChanged();
		});

		// force initial update
		m_wave = xt::WaveId(0);
		setWave(xt::WaveId());
	}

	void ControlTreeItem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeItem::setNode(_node);
	}

	xt::TableIndex ControlTreeItem::getTableIndex() const
	{
		if (auto* node = dynamic_cast<ControlTreeNode*>(getNode().get()))
			return node->getIndex();
		return xt::TableIndex::invalid();
	}

	void ControlTreeItem::paintItem(juce::Graphics& _g, const int _width, const int _height)
	{
		if(const auto wave = m_editor.getData().getWave(m_wave))
			WaveTreeItem::paintWave(*wave, _g, 0, 0, _width, _height, 0xffffffff);
	}

	void ControlTreeItem::setWave(const xt::WaveId _wave)
	{
		m_wave = _wave;
		const auto name = WaveTreeItem::getWaveName(m_wave);
		char prefix[16] = {};
		(void)snprintf(prefix, std::size(prefix), "%02d: ", getTableIndex().rawId());
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
			if (getTableIndex().rawId() == 2)
				setWave(m_editor.getData().getWaveId(_table, getTableIndex()));
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
			switch (getTableIndex().rawId())
			{
			case 61: setWave(xt::WaveId(101)); break;
			case 62: setWave(xt::WaveId(100)); break;
			case 63: setWave(xt::WaveId(104)); break;
			default: setWave(m_editor.getData().getWaveId(_table, getTableIndex())); break;
			}
		}
	}

	std::unique_ptr<juceRmlUi::DragData> ControlTreeItem::createDragData()
	{
		if(m_wave == g_invalidWaveIndex || xt::wave::isReadOnly(m_table) || xt::wave::isReadOnly(getTableIndex()))
			return {};

		auto desc = std::make_unique<WaveDesc>(m_editor);

		desc->waveIds = {m_wave};
		desc->source = WaveDescSource::ControlTableList;
		desc->tableIndex = getTableIndex();

		desc->fillData(m_editor.getData());

		return desc;
	}

	bool ControlTreeItem::canDrop(const Rml::Event& _event, const DragSource* _source)
	{
		if(xt::wave::isReadOnly(m_table) || xt::wave::isReadOnly(getTableIndex()))
			return TreeItem::canDrop(_event, _source);
		auto* desc = WaveDesc::fromDragSource(_source);
		if (desc == nullptr)
			return TreeItem::canDrop(_event, _source);
		if (desc->source == WaveDescSource::WaveList && desc->waveIds.size() != 1)
			return TreeItem::canDrop(_event, _source);
		if (desc->source == WaveDescSource::ControlTableList && desc->tableIndex == getTableIndex())
			return TreeItem::canDrop(_event, _source);
		return true;
	}

	void ControlTreeItem::drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data)
	{
		const auto* waveDesc = WaveDesc::fromDragSource(_source);
		if(!waveDesc)
			return;

		auto& data = m_editor.getData();

		// if the source is the control list, we swap two entries. if the source is the wave list, we add a new wave
		if(waveDesc->source == WaveDescSource::ControlTableList)
		{
			if(data.swapTableEntries(m_table, getTableIndex(), waveDesc->tableIndex))
			{
				getNode()->setSelected(true, false);
				data.sendTableToDevice(m_table);
			}
		}
		else if(waveDesc->source == WaveDescSource::WaveList && waveDesc->waveIds.size() == 1)
		{
			if(data.setTableWave(m_table, getTableIndex(), waveDesc->waveIds.front()))
				data.sendTableToDevice(m_table);
		}
	}

	void ControlTreeItem::openContextMenu(const Rml::Event& _event)
	{
		juceRmlUi::Menu menu;

		menu.addEntry("Remove", [this]
		{
			if (m_editor.getData().setTableWave(m_table, getTableIndex(), g_invalidWaveIndex))
				m_editor.getData().sendTableToDevice(m_table);
		});
		menu.addEntry("Select Wave", [this]
		{
			m_editor.setSelectedWave(m_wave);
		});
		menu.addSubMenu("Copy to", std::move(m_editor.createCopyToSelectedTableMenu(m_wave)));

		menu.runModal(_event);
	}

	void ControlTreeItem::onWaveChanged() const
	{
		repaintItem();
	}
}
