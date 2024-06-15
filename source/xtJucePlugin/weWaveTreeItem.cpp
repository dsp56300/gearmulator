#include "weWaveTreeItem.h"

#include "weWaveCategoryTreeItem.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	WaveTreeItem::WaveTreeItem(WaveEditor& _editor, const WaveCategory _category, const uint32_t _waveIndex)
		: m_editor(_editor)
		, m_category(_category)
		, m_waveIndex(_waveIndex)
	{
		m_onWaveChanged.set(m_editor.getData().onWaveChanged, [this](const unsigned& _waveIndex)
		{
			onWaveChanged(_waveIndex);
		});

		setText(getWaveName(_waveIndex));
	}

	void WaveTreeItem::paintWave(const WaveData& _data, juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height, const juce::Colour& _colour)
	{
		_g.setColour(_colour);

		const float scaleX = static_cast<float>(_width)  / static_cast<float>(_data.size());
		const float scaleY = static_cast<float>(_height) / static_cast<float>(256);

		for(uint32_t x=1; x<_data.size(); ++x)
		{
			const auto x0 = static_cast<float>(x - 1) * scaleX + static_cast<float>(_x);
			const auto x1 = static_cast<float>(x    ) * scaleX + static_cast<float>(_x);

			const auto y0 = static_cast<float>(255 - (_data[x - 1] + 128)) * scaleY + static_cast<float>(_y);
			const auto y1 = static_cast<float>(255 - (_data[x    ] + 128)) * scaleY + static_cast<float>(_y);

			_g.drawLine(x0, y0, x1, y1);
		}
	}

	std::string WaveTreeItem::getWaveName(const uint32_t _waveIndex)
	{
		if(!xt::Wave::isValidWaveIndex(_waveIndex))
			return {};
		const auto category = getCategory(_waveIndex);
		return WaveCategoryTreeItem::getCategoryName(category) + ' ' + std::to_string(_waveIndex);
	}

	WaveCategory WaveTreeItem::getCategory(const uint32_t _waveIndex)
	{
		if(_waveIndex < xt::Wave::g_romWaveCount)
			return WaveCategory::Rom;
		if(_waveIndex >= xt::Wave::g_firstRamWaveIndex && _waveIndex < xt::Wave::g_firstRamWaveIndex + xt::Wave::g_ramWaveCount)
			return WaveCategory::User;
		return WaveCategory::Invalid;
	}

	void WaveTreeItem::itemSelectionChanged(bool isNowSelected)
	{
		TreeItem::itemSelectionChanged(isNowSelected);

		if(isNowSelected)
			m_editor.setSelectedWave(m_waveIndex);
	}

	void WaveTreeItem::onWaveChanged(const uint32_t _index)
	{
		if(_index != m_waveIndex)
			return;
		onWaveChanged();
	}

	void WaveTreeItem::onWaveChanged()
	{
		repaintItem();
	}

	void WaveTreeItem::paintItem(juce::Graphics& g, int width, int height)
	{
		if(const auto wave = m_editor.getData().getWave(m_waveIndex))
			paintWave(*wave, g, width>>1, 0, width>>1, height, juce::Colour(0xffffffff));

		TreeItem::paintItem(g, width, height);
	}
}
