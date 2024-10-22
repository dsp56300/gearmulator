#include "weWaveCategoryTreeItem.h"

#include "weWaveTreeItem.h"
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

	void WaveCategoryTreeItem::addItems(uint32_t _first, uint32_t _count)
	{
		for(uint32_t i=0; i<_count; ++i)
			addItem(i + _first);

		setOpen(true);
	}

	void WaveCategoryTreeItem::addItem(const uint32_t _index)
	{
		addSubItem(new WaveTreeItem(m_editor, m_category, xt::WaveId(_index)));
	}
}
