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
		ControlTreeItem(WaveEditor& _editor, xt::TableIndex _index);

		bool mightContainSubItems() override { return false; }

		void paintItem(juce::Graphics& _g, int _width, int _height) override;

		void setWave(xt::WaveId _wave);
		void setTable(xt::TableId _table, bool _tableHasChanged);
		juce::var getDragSourceDescription() override;
		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) override;
		void itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails, int _insertIndex) override;

	private:
		void onWaveChanged() const;

		WaveEditor& m_editor;
		const xt::TableIndex m_index;

		xt::WaveId m_wave = g_invalidWaveIndex;
		xt::TableId m_table = g_invalidTableIndex;

		pluginLib::EventListener<xt::WaveId> m_onWaveChanged;
	};
}
