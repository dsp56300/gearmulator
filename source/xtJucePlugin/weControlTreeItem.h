#pragma once

#include "weTreeItem.h"

#include "jucePluginLib/event.h"

namespace xtJucePlugin
{
	class WaveEditor;

	class ControlTreeItem : public TreeItem
	{
	public:
		ControlTreeItem(WaveEditor& _editor, uint32_t _index);

		bool mightContainSubItems() override { return false; }

		void paintItem(juce::Graphics& _g, int _width, int _height) override;

		void setWave(uint32_t _wave);
		void setTable(uint32_t _table);

	private:
		void onWaveChanged() const;

		WaveEditor& m_editor;
		const uint32_t m_index;

		uint32_t m_wave = ~0;
		uint32_t m_table = ~0;

		pluginLib::EventListener<uint32_t> m_onWaveChanged;
	};
}
