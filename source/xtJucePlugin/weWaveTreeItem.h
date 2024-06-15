#pragma once

#include "weTreeItem.h"
#include "weTypes.h"

#include "../jucePluginLib/event.h"

namespace xtJucePlugin
{
	enum class WaveCategory;
	class WaveEditor;

	class WaveTreeItem : public TreeItem
	{
	public:
		WaveTreeItem(WaveEditor& _editor, WaveCategory _category, uint32_t _waveIndex);

		bool mightContainSubItems() override { return false; }

		static void paintWave(const WaveData& _data, juce::Graphics& _g, int _width, int _height, juce::Colour _colour);

	private:
		void onWaveChanged(uint32_t _index);
		void onWaveChanged();

		void paintItem(juce::Graphics& g, int width, int height) override;

		WaveEditor& m_editor;
		WaveCategory m_category;
		const uint32_t m_waveIndex;
		pluginLib::EventListener<uint32_t> m_onWaveChanged;
	};
}
