#pragma once

#include "weTreeItem.h"
#include "weTypes.h"

namespace xtJucePlugin
{
	class WaveEditor;

	class WaveCategoryNode : public juceRmlUi::TreeNode
	{
	public:
		WaveCategoryNode(juceRmlUi::Tree& _tree, const WaveCategory _category) : TreeNode(_tree), m_category(_category)
		{
		}
		~WaveCategoryNode() override = default;
		WaveCategory getCategory() const { return m_category; }

	private:
		const WaveCategory m_category;
	};

	class WaveCategoryTreeItem : public TreeItem
	{
	public:
		explicit WaveCategoryTreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor);
		bool setSelectedWave(xt::WaveId _id);

		static std::string getCategoryName(WaveCategory _category);

		std::unique_ptr<juceRmlUi::DragData> createDragData() override;

		void openContextMenu(const Rml::Event& _event) override;

		WaveCategory getCategory() const;

		void setNode(const juceRmlUi::TreeNodePtr& _node) override;

		void paintItem(juce::Graphics& _g, int _width, int _height) override;

	private:
		void addItems(uint16_t _first, uint16_t _count);
		void addItem(uint16_t _index);

		void exportAll(bool _midi) const;

		std::vector<xt::WaveId> getWaveIds() const;

		WaveEditor& m_editor;
	};
}
