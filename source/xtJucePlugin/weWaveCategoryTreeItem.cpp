#include "weWaveCategoryTreeItem.h"

#include "weWaveDesc.h"
#include "weWaveTreeItem.h"
#include "xtWaveEditor.h"

#include "xtLib/xtMidiTypes.h"

namespace xtJucePlugin
{
	constexpr const char* const g_categoryNames[] =
	{
		"ROM",
		"User",
		"Plugin"
	};

	static_assert(std::size(g_categoryNames) == static_cast<size_t>(WaveCategory::Count));

	WaveCategoryTreeItem::WaveCategoryTreeItem(WaveEditor& _editor, const WaveCategory _category) : m_editor(_editor), m_category(_category)
	{
		setText(getCategoryName(_category));

		switch (_category)
		{
		case WaveCategory::Rom:
			addItems(0, xt::wave::g_romWaveCount);
			break;
		case WaveCategory::User:
			addItems(xt::wave::g_firstRamWaveIndex, xt::wave::g_ramWaveCount);
			break;
		case WaveCategory::Invalid:
		case WaveCategory::Plugin:
		case WaveCategory::Count:
		default:
			break;
		}
	}

	bool WaveCategoryTreeItem::setSelectedWave(const xt::WaveId _id)
	{
		for(int i=0; i<getNumSubItems(); ++i)
		{
			auto* subItem = dynamic_cast<WaveTreeItem*>(getSubItem(i));
			if(subItem && subItem->getWaveId() == _id)
			{
				subItem->setSelected(true, true, juce::dontSendNotification);
				setOpen(true);
				getOwnerView()->scrollToKeepItemVisible(subItem);
				return true;
			}
		}
		return false;
	}

	std::string WaveCategoryTreeItem::getCategoryName(WaveCategory _category)
	{
		return g_categoryNames[static_cast<uint32_t>(_category)];
	}

	juce::var WaveCategoryTreeItem::getDragSourceDescription()
	{
		auto* desc = new WaveDesc(m_editor);

		desc->waveIds = getWaveIds();
		desc->source = WaveDescSource::WaveList;

		desc->fillData(m_editor.getData());

		return desc;
	}

	void WaveCategoryTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if (_mouseEvent.mods.isPopupMenu())
		{
			if (m_category != WaveCategory::Rom)
			{
				juce::PopupMenu menu;
				menu.addItem("Export all as .syx", [this]
				{
					exportAll(false);
				});
				menu.addItem("Export all as .mid", [this]
				{
					exportAll(true);
				});
				menu.showMenuAsync({});
				return;
			}
		}
		TreeItem::itemClicked(_mouseEvent);
	}

	void WaveCategoryTreeItem::addItems(const uint16_t _first, const uint16_t _count)
	{
		for(uint16_t i=_first; i<_first+_count; ++i)
			addItem(i);

		setOpen(true);
	}

	void WaveCategoryTreeItem::addItem(const uint16_t _index)
	{
		addSubItem(new WaveTreeItem(m_editor, m_category, xt::WaveId(_index)));
	}

	void WaveCategoryTreeItem::exportAll(const bool _midi) const
	{
		auto waveIds = getWaveIds();

		m_editor.exportAsSyxOrMid(waveIds, _midi);
	}

	std::vector<xt::WaveId> WaveCategoryTreeItem::getWaveIds() const
	{
		std::vector<xt::WaveId> waveIds;
		waveIds.reserve(getNumSubItems());
		for (int i = 0; i < getNumSubItems(); ++i)
		{
			if (const auto* subItem = dynamic_cast<WaveTreeItem*>(getSubItem(i)))
				waveIds.push_back(subItem->getWaveId());
		}
		return waveIds;
	}
}
