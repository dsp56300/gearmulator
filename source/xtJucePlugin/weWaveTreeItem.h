#pragma once

#include "weTreeItem.h"
#include "weTypes.h"

#include "baseLib/event.h"

namespace juce
{
	class StringArray;
	class Colour;
	class Graphics;
}

namespace xtJucePlugin
{
	enum class WaveCategory;
	class WaveEditor;

	class WaveTreeNode : public juceRmlUi::TreeNode
	{
	public:
		WaveTreeNode(juceRmlUi::Tree& _tree, xt::WaveId _waveIndex) : TreeNode(_tree), m_waveIndex(_waveIndex)
		{
		}
		~WaveTreeNode() override = default;
		xt::WaveId getWaveId() const { return m_waveIndex; }

	private:
		const xt::WaveId m_waveIndex;
	};

	class WaveTreeItem : public TreeItem
	{
	public:
		WaveTreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor);

		xt::WaveId getWaveId() const;

		void setNode(const juceRmlUi::TreeNodePtr& _node) override;

		static void paintWave(const xt::WaveData& _data, juce::Graphics& _g, int _x, int _y, int _width, int _height, const juce::Colour& _colour);

		static std::string getWaveName(xt::WaveId _waveIndex);
		static WaveCategory getCategory(xt::WaveId _waveIndex);

		void onSelectedChanged(bool _selected) override;
		void onRightClick(const Rml::Event& _event) override;

		std::unique_ptr<juceRmlUi::DragData> createDragData() override;

		bool canDrop(const Rml::Event& _event, const DragSource* _source) override;
		void drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data) override;
		bool canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files) override;
		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files) override;
		void dropFiles(const std::vector<std::string>& _files) const;

		static std::vector<std::vector<uint8_t>> getSysexFromFiles(const std::vector<std::string>& _files);

		void paintItem(juce::Graphics& _g, int _width, int _height) override;

	private:
		void onWaveChanged(xt::WaveId _index) const;
		void onWaveChanged() const;

		WaveEditor& m_editor;
		WaveCategory m_category;
		baseLib::EventListener<xt::WaveId> m_onWaveChanged;
	};
}
