#include "weControlTreeItem.h"

#include "weWaveTreeItem.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	class WaveEditor;

	ControlTreeItem::ControlTreeItem(WaveEditor& _editor, const uint32_t _index) : m_editor(_editor), m_index(_index)
	{
		m_onWaveChanged.set(_editor.getData().onWaveChanged, [this](const unsigned& _wave)
		{
			if(_wave == m_wave)
				onWaveChanged();
		});

		setPaintRootItemInBold(false);
		setDrawsInLeftMargin(true);
	}

	void ControlTreeItem::paintItem(juce::Graphics& _g, const int _width, const int _height)
	{
		if(const auto wave = m_editor.getData().getWave(m_table, m_index))
			WaveTreeItem::paintWave(*wave, _g, _width>>1, 0, _width>>1, _height, juce::Colour(0xffffffff));

		TreeItem::paintItem(_g, _width, _height);
	}

	void ControlTreeItem::setWave(const uint32_t _wave)
	{
		if(m_wave == _wave)
			return;
		m_wave = _wave;
		const auto name = WaveTreeItem::getWaveName(m_wave);
		char prefix[16] = {0};
		snprintf(prefix, std::size(prefix), "%02d: ", m_index);
		setText(prefix + (name.empty() ? "-" : name));
		repaintItem();
	}

	void ControlTreeItem::setTable(const uint32_t _table)
	{
		if(m_table == _table)
			return;
		m_table = _table;
		setWave(m_editor.getData().getWaveIndex(_table, m_index));
	}

	void ControlTreeItem::onWaveChanged() const
	{
		repaintItem();
	}
}
