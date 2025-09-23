#pragma once

#include "weTreeItem.h"
#include "weTypes.h"

#include "baseLib/event.h"

namespace xtJucePlugin
{
	class WaveEditor;

	class ControlTreeNode : public juceRmlUi::TreeNode
	{
	public:
		ControlTreeNode(juceRmlUi::Tree& _tree, const xt::TableIndex _index) : TreeNode(_tree), m_index(_index)
		{
		}
		~ControlTreeNode() override = default;
		xt::TableIndex getIndex() const { return m_index; }

	private:
		const xt::TableIndex m_index;
	};

	class ControlTreeItem : public TreeItem
	{
	public:
		ControlTreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor);

		void paintItem(juce::Graphics& _g, int _width, int _height) override;

		void setNode(const juceRmlUi::TreeNodePtr& _node) override;

		xt::TableIndex getTableIndex() const;

		void setWave(xt::WaveId _wave);
		void setTable(xt::TableId _table, bool _tableHasChanged);

		std::unique_ptr<juceRmlUi::DragData> createDragData() override;

		bool canDrop(const Rml::Event& _event, const DragSource* _source) override;
		void drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data) override;

		void onRightClick(const Rml::Event& _event) override;

	private:
		void onWaveChanged() const;

		WaveEditor& m_editor;

		xt::WaveId m_wave = g_invalidWaveIndex;
		xt::TableId m_table = g_invalidTableIndex;

		baseLib::EventListener<xt::WaveId> m_onWaveChanged;
	};
}
