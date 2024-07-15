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
			addItems(0, xt::Wave::g_romWaveCount);
			break;
		case WaveCategory::User:
			addItems(xt::Wave::g_firstRamWaveIndex, xt::Wave::g_ramWaveCount);
			break;
		case WaveCategory::Invalid:
		case WaveCategory::Plugin:
		case WaveCategory::Count:
		default:
			break;
		}
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
		addSubItem(new WaveTreeItem(m_editor, m_category, _index));
	}
}
