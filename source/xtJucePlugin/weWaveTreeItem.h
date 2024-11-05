#pragma once

#include "weTreeItem.h"
#include "weTypes.h"

#include "jucePluginLib/event.h"

namespace xtJucePlugin
{
	enum class WaveCategory;
	class WaveEditor;

	class WaveTreeItem : public TreeItem
	{
	public:
		WaveTreeItem(WaveEditor& _editor, WaveCategory _category, xt::WaveId _waveIndex);

		auto getWaveId() const { return m_waveIndex; }

		bool mightContainSubItems() override { return false; }

		static void paintWave(const xt::WaveData& _data, juce::Graphics& _g, int _x, int _y, int _width, int _height, const juce::Colour& _colour);

		static std::string getWaveName(xt::WaveId _waveIndex);
		static WaveCategory getCategory(xt::WaveId _waveIndex);

		void itemSelectionChanged(bool isNowSelected) override;

		juce::var getDragSourceDescription() override;
		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
		void itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex) override;

		bool isInterestedInFileDrag(const juce::StringArray& files) override;
		void filesDropped(const juce::StringArray& files, int insertIndex) override;

		static std::vector<std::vector<uint8_t>> getSysexFromFiles(const juce::StringArray& _files);

		void itemClicked(const juce::MouseEvent&) override;

	private:
		void onWaveChanged(xt::WaveId _index) const;
		void onWaveChanged() const;

		void paintItem(juce::Graphics& g, int width, int height) override;

		WaveEditor& m_editor;
		WaveCategory m_category;
		const xt::WaveId m_waveIndex;
		pluginLib::EventListener<xt::WaveId> m_onWaveChanged;
	};
}
