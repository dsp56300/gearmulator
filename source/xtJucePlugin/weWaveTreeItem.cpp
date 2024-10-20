#include "weWaveTreeItem.h"

#include "weWaveCategoryTreeItem.h"
#include "weWaveDesc.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	WaveTreeItem::WaveTreeItem(WaveEditor& _editor, const WaveCategory _category, const xt::WaveId _waveIndex)
		: m_editor(_editor)
		, m_category(_category)
		, m_waveIndex(_waveIndex)
	{
		m_onWaveChanged.set(m_editor.getData().onWaveChanged, [this](const xt::WaveId& _waveIndex)
		{
			onWaveChanged(_waveIndex);
		});

		setText(getWaveName(_waveIndex));
	}

	void WaveTreeItem::paintWave(const xt::WaveData& _data, juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height, const juce::Colour& _colour)
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

	std::string WaveTreeItem::getWaveName(const xt::WaveId _waveIndex)
	{
		if(!xt::wave::isValidWaveIndex(_waveIndex.rawId()))
			return {};
		const auto category = getCategory(_waveIndex);
		return WaveCategoryTreeItem::getCategoryName(category) + ' ' + std::to_string(_waveIndex.rawId());
	}

	WaveCategory WaveTreeItem::getCategory(const xt::WaveId _waveIndex)
	{
		if(_waveIndex.rawId() < xt::wave::g_romWaveCount)
			return WaveCategory::Rom;
		if(_waveIndex.rawId() >= xt::wave::g_firstRamWaveIndex && _waveIndex.rawId() < xt::wave::g_firstRamWaveIndex + xt::wave::g_ramWaveCount)
			return WaveCategory::User;
		return WaveCategory::Invalid;
	}

	void WaveTreeItem::itemSelectionChanged(bool isNowSelected)
	{
		TreeItem::itemSelectionChanged(isNowSelected);

		if(isNowSelected)
			m_editor.setSelectedWave(m_waveIndex);
	}

	juce::var WaveTreeItem::getDragSourceDescription()
	{
		auto* desc = new WaveDesc();
		desc->waveId = m_waveIndex;
		desc->source = WaveDescSource::WaveList;
		return desc;
	}

	bool WaveTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
	{
		if(xt::wave::isReadOnly(m_waveIndex))
			return false;
		const auto* waveDesc = WaveDesc::fromDragSource(dragSourceDetails);
		if(!waveDesc)
			return false;
		if(!waveDesc->waveId.isValid())
			return false;
		return true;
	}

	void WaveTreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex)
	{
		TreeItem::itemDropped(dragSourceDetails, insertIndex);
		if(xt::wave::isReadOnly(m_waveIndex))
			return;
		const auto* waveDesc = WaveDesc::fromDragSource(dragSourceDetails);
		if(!waveDesc)
			return;
		auto& data = m_editor.getData();

		if(data.copyWave(m_waveIndex, waveDesc->waveId))
		{
			setSelected(true, true, juce::dontSendNotification);
			data.sendWaveToDevice(m_waveIndex);
		}
	}

	void WaveTreeItem::onWaveChanged(const xt::WaveId _index) const
	{
		if(_index != m_waveIndex)
			return;
		onWaveChanged();
	}

	void WaveTreeItem::onWaveChanged() const
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
