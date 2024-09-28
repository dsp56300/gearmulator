#pragma once

#include "weTreeItem.h"
#include "weTypes.h"

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
		void setTable(uint32_t _table, bool _tableHasChanged);
		juce::var getDragSourceDescription() override;
		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) override;
		void itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails, int _insertIndex) override;

	private:
		void onWaveChanged() const;

		WaveEditor& m_editor;
		const uint32_t m_index;

		uint32_t m_wave = g_invalidWaveIndex;
		uint32_t m_table = g_invalidTableIndex;

		pluginLib::EventListener<uint32_t> m_onWaveChanged;
	};
}
