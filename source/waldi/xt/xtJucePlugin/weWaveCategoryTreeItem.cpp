#include "weWaveCategoryTreeItem.h"

#include "weWaveDesc.h"
#include "weWaveTreeItem.h"
#include "xtWaveEditor.h"
#include "juceRmlUi/rmlElemTree.h"
#include "juceRmlUi/rmlMenu.h"

#include "juce_graphics/juce_graphics.h"

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

	WaveCategoryTreeItem::WaveCategoryTreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor)
	: TreeItem(_coreInstance, _tag)
	, m_editor(_editor)
	{
	}

	bool WaveCategoryTreeItem::setSelectedWave(const xt::WaveId _id)
	{
		for(size_t i=0; i<getNode()->size(); ++i)
		{
			auto child = getNode()->getChild(i);
			auto* subItem = dynamic_cast<WaveTreeItem*>(child->getElement());
			if(subItem && subItem->getWaveId() == _id)
			{
				subItem->getNode()->setSelected(true);
				getNode()->setOpened(true);

				Rml::ScrollIntoViewOptions options;

				options.vertical = Rml::ScrollAlignment::Nearest;
				options.horizontal = Rml::ScrollAlignment::Nearest;
				options.behavior = Rml::ScrollBehavior::Smooth;
				options.parentage = Rml::ScrollParentage::All;

				subItem->ScrollIntoView(options);

				return true;
			}
		}
		return false;
	}

	std::string WaveCategoryTreeItem::getCategoryName(WaveCategory _category)
	{
		return g_categoryNames[static_cast<uint32_t>(_category)];
	}

	std::unique_ptr<juceRmlUi::DragData> WaveCategoryTreeItem::createDragData()
	{
		std::unique_ptr<WaveDesc> desc = std::make_unique<WaveDesc>(m_editor);

		desc->waveIds = getWaveIds();
		desc->source = WaveDescSource::WaveList;

		desc->fillData(m_editor.getData());

		return desc;
	}

	void WaveCategoryTreeItem::openContextMenu(const Rml::Event& _event)
	{
		if (getCategory() == WaveCategory::Rom)
			return;

		juceRmlUi::Menu menu;
		menu.addEntry("Export all as .syx", [this]
		{
			exportAll(false);
		});
		menu.addEntry("Export all as .mid", [this]
		{
			exportAll(true);
		});
		menu.runModal(_event);
	}

	WaveCategory WaveCategoryTreeItem::getCategory() const
	{
		if (const auto* node = dynamic_cast<const WaveCategoryNode*>(getNode().get()))
			return node->getCategory();
		return WaveCategory::Invalid;
	}

	void WaveCategoryTreeItem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeItem::setNode(_node);

		const auto category = getCategory();

		setText(getCategoryName(category));

		switch (category)
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

	void WaveCategoryTreeItem::paintItem(juce::Graphics& _g, const int _width, const int _height)
	{
		hideCanvas();
		_g.fillAll(juce::Colours::magenta);
	}

	void WaveCategoryTreeItem::addItems(const uint16_t _first, const uint16_t _count)
	{
		for(uint16_t i=_first; i<_first+_count; ++i)
			addItem(i);

		getNode()->setOpened(true);
	}

	void WaveCategoryTreeItem::addItem(const uint16_t _index)
	{
		getNode()->createChild<WaveTreeNode>(xt::WaveId(_index));
//		addSubItem();
	}

	void WaveCategoryTreeItem::exportAll(const bool _midi) const
	{
		auto waveIds = getWaveIds();

		m_editor.exportAsSyxOrMid(waveIds, _midi);
	}

	std::vector<xt::WaveId> WaveCategoryTreeItem::getWaveIds() const
	{
		std::vector<xt::WaveId> waveIds;
		waveIds.reserve(getNode()->size());
		for (int i = 0; i < getNode()->size(); ++i)
		{
			const auto child = getNode()->getChild(i);
			if (const auto* subItem = dynamic_cast<WaveTreeItem*>(child->getElement()))
				waveIds.push_back(subItem->getWaveId());
		}
		return waveIds;
	}
}
