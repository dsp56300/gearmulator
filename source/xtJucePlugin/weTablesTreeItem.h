#pragma once

#include "weTreeItem.h"

#include "baseLib/event.h"

#include "xtLib/xtId.h"

namespace xtJucePlugin
{
	class WaveEditor;

	class TablesTreeNode : public juceRmlUi::TreeNode
	{
	public:
		TablesTreeNode(juceRmlUi::Tree& _tree, const xt::TableId _tableIndex) : TreeNode(_tree), m_tableIndex(_tableIndex)
		{
		}

		xt::TableId getTableId() const { return m_tableIndex; }

	private:
		const xt::TableId m_tableIndex;
	};

	class TablesTreeItem : public TreeItem
	{
	public:
		TablesTreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor);

		void setNode(const juceRmlUi::TreeNodePtr& _node) override;

		xt::TableId getTableId() const;

		void onSelectedChanged(bool _selected) override;

		std::unique_ptr<juceRmlUi::DragData> createDragData() override;

		bool canDrop(const Rml::Event& _event, const DragSource* _source) override;
		void drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data) override;

		bool canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files) override;
		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files) override;
		void dropFiles(const std::vector<std::string>& _files);

		void onRightClick(const Rml::Event& _event) override;

		void paintItem(juce::Graphics& _g, const int _width, const int _height) override;

	private:
		void onTableChanged(xt::TableId _index);
		void onTableChanged();

		WaveEditor& m_editor;
		baseLib::EventListener<xt::TableId> m_onTableChanged;
	};
}
