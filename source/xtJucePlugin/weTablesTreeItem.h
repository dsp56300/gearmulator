#pragma once

#include "weTreeItem.h"

#include "jucePluginLib/event.h"

#include "xtLib/xtId.h"

namespace xtJucePlugin
{
	class WaveEditor;

	class TablesTreeItem : public TreeItem
	{
	public:
		TablesTreeItem(WaveEditor& _editor, xt::TableId _tableIndex);

		bool mightContainSubItems() override { return false; }

		void itemSelectionChanged(bool _isNowSelected) override;

		juce::var getDragSourceDescription() override;
		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
		void itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex) override;

		bool isInterestedInFileDrag(const juce::StringArray& files) override;
		void filesDropped(const juce::StringArray& files, int insertIndex) override;

		void itemClicked(const juce::MouseEvent&) override;

		const auto& getTableId() const { return m_index; }

		juce::Colour getTextColor(const juce::Colour _colour) override;
	private:
		void onTableChanged(xt::TableId _index);
		void onTableChanged();

		WaveEditor& m_editor;
		const xt::TableId m_index;
		pluginLib::EventListener<xt::TableId> m_onTableChanged;
	};
}
