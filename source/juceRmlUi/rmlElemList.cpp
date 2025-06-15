#include "rmlElemList.h"

#include "rmlElemListEntry.h"
#include "rmlHelper.h"
#include "rmlListEntry.h"

#include "RmlUi/Core/ElementDocument.h"

using namespace Rml;

namespace juceRmlUi
{
	ElemList::ElemList(const String& _tag) : Element(_tag)
	{
		for (size_t i=0; i<700; ++i)
		{
			auto e = std::make_shared<ListEntry>(m_list);
			m_list.addEntry(std::move(e));
		}
	}

	void ElemList::OnChildAdd(Rml::Element* _child)
	{
		Element::OnChildAdd(_child);
	}

	void ElemList::OnUpdate()
	{
		Element::OnUpdate();

		if (m_spacerTL == nullptr || m_spacerBR == nullptr || m_entryTemplate == nullptr)
		{
			initialize();
		}

		if (m_layoutDirty)
		{
			if (m_layoutDirty > 1)
				--m_layoutDirty;
			else if (updateLayout())
				m_layoutDirty = 0;
		}
	}

	void ElemList::OnDpRatioChange()
	{
		Element::OnDpRatioChange();
		m_layoutDirty = 2;	// the values are not yet updated in the next OnUpdate, delay further
	}

	void ElemList::OnLayout()
	{
		Element::OnLayout();
	}

	void ElemList::OnResize()
	{
		Element::OnResize();

		if (m_entryTemplate && getItemsPerColumn() != m_lastItemsPerColumn)
			m_layoutDirty = 1;
	}

	void ElemList::ProcessEvent(Event& _event)
	{
		switch (_event.GetId())
		{
		case EventId::Scroll:
			onScroll(_event);
			break;
		default:;
		}
	}

	void ElemList::initialize()
	{
		if (!GetNumChildren())
			return;

		if (getLayoutType() == LayoutType::None)
			return;

		for (int i=0; i<GetNumChildren(); ++i)
		{
			const auto e = GetChild(i);
			m_entryTemplate = dynamic_cast<ElemListEntry*>(e);
			if (m_entryTemplate)
				break;
		}

		if (m_entryTemplate == nullptr)
		{
			Log::Message(Log::LT_ERROR, "list needs a child of type 'listitem'");
			return;
		}

		m_spacerTL = createSpacer();
		m_spacerBR = createSpacer();

		AddEventListener(EventId::Scroll, this);

//		Rml::Debugger::Initialise(GetContext());
//		Rml::Debugger::SetVisible(true);
	}

	Vector2f ElemList::updateElementSize()
	{
		if (!m_activeEntries.empty())
		{
			auto size = m_activeEntries.begin()->second->GetBox().GetSize(BoxArea::Margin);

			if (size.x > 0 && size.y > 0)
			{
				m_elementSize = size;
				return size;
			}
		}
		if (m_elementSize.x <= 0.0f || m_elementSize.y <= 0 && m_entryTemplate->GetDisplay() != Style::Display::None)
		{
			m_elementSize = m_entryTemplate->GetBox().GetSize(BoxArea::Margin);
			if (m_elementSize.x > 0 && m_elementSize.y > 0)
			{
				if (getLayoutType() == LayoutType::Grid)
					m_entryTemplatePtr = m_entryTemplate->GetParentNode()->RemoveChild(m_entryTemplate);
				else
					m_entryTemplate->SetProperty(PropertyId::Display, Property(Style::Display::None));
				return m_elementSize;
			}
		}
		return m_elementSize;
	}

	bool ElemList::updateLayout()
	{
		switch (getLayoutType())
		{
		case LayoutType::List:
			return updateLayoutVertical();
		case LayoutType::Grid:
			return updateLayoutGrid();
		case LayoutType::None:
		default:
			return false;
		}
	}

	bool ElemList::updateLayoutVertical()
	{
		const auto box = GetBox();
		const auto size = box.GetSize(BoxArea::Content);
		const auto scrollTop = GetScrollTop();

		const auto elementHeight = updateElementSize().y;
		
		if (elementHeight <= 0.0f)
			return false;

		const auto firstEntry = static_cast<size_t>(scrollTop / elementHeight);
		const auto lastEntry = static_cast<size_t>(std::ceil((scrollTop + size.y) / elementHeight));

		updateActiveEntries(firstEntry, lastEntry, false);

		setSpacerTL(static_cast<float>(firstEntry) * elementHeight);
		setSpacerBR(lastEntry < m_list.size() ? static_cast<float>(m_list.size() - lastEntry) * elementHeight : 0);

		return true;
	}

	bool ElemList::updateLayoutGrid()
	{
		const auto box = GetBox();
		const auto size = box.GetSize(BoxArea::Content);
		const auto scrollLeft = GetScrollLeft();

		const auto elementSize = updateElementSize();

		if (elementSize.x <= 0 || elementSize.y <= 0)
			return false;

		const auto itemsPerColumn = getItemsPerColumn();

		if (!itemsPerColumn)
			return false;

		const auto itemsPerColumnChanged = m_lastItemsPerColumn != itemsPerColumn;
		m_lastItemsPerColumn = itemsPerColumn;

		const auto totalColumns = (m_list.size() + itemsPerColumn - 1) / itemsPerColumn;

		const auto firstColumn = static_cast<size_t>(scrollLeft / elementSize.x);
		const auto lastColumn = std::min(static_cast<size_t>(std::ceil((scrollLeft + size.x) / elementSize.x)), totalColumns - 1);

		const auto firstEntry = firstColumn * itemsPerColumn;
		const auto lastEntry = lastColumn * itemsPerColumn + itemsPerColumn - 1;

		bool changed = updateActiveEntries(firstEntry, lastEntry, itemsPerColumnChanged);

		changed |= updateActiveColumns(firstColumn, lastColumn, itemsPerColumn, itemsPerColumnChanged);

		// if the DP ratio changes, the element size will change. Ensure that columns have the correct width
		for (auto it : m_activeColumns)
		{
			if (!helper::changeProperty(it.second, PropertyId::FlexBasis, Property(elementSize.x, Unit::PX)))
				break;
			changed = true;
		}

		if (changed)
		{
			setSpacerTL(static_cast<float>(firstColumn) * elementSize.x);
			setSpacerBR(lastColumn + 1 < totalColumns ? static_cast<float>(totalColumns - lastColumn - 1) * elementSize.x : 0);
		}

		return true;
	}

	bool ElemList::updateActiveEntries(const size_t _firstEntry, size_t _lastEntry, bool _forceRefresh)
	{
		bool dirty = false;

		const auto layoutType = getLayoutType();

		// return elements back to pool that we don't need
		for (auto it = m_activeEntries.begin(); it != m_activeEntries.end();)
		{
			const auto i = it->first;
			auto& entry = it->second;

			const auto isInRange = !_forceRefresh && i >= _firstEntry && i <= _lastEntry;

			if (!isInRange)
			{
				auto parent = entry->GetParentNode();
				assert(parent);
				auto e = parent->RemoveChild(entry);
				m_inactiveEntries.emplace_back(std::move(e));
				it = m_activeEntries.erase(it);
				dirty = true;
			}
			else
			{
				++it;
			}
		}

		// create new elements for entries that are not active yet
		if (_lastEntry >= m_list.size())
			_lastEntry = m_list.size() - 1;

		for (size_t i = _firstEntry; i <= _lastEntry; ++i)
		{
			if (m_activeEntries.find(i) != m_activeEntries.end())
				continue;

			dirty = true;

			ElementPtr e;
			if (!m_inactiveEntries.empty())
			{
				e = std::move(m_inactiveEntries.back());
				m_inactiveEntries.pop_back();
			}
			else
			{
				e = m_entryTemplate->Clone();

				e->SetProperty(PropertyId::Display, Property(Style::Display::Block));
			}

			auto entry = dynamic_cast<ElemListEntry*>(e.get());
			if (entry)
				entry->setListItem(m_list.getEntry(i));

			// if grid layout is active, items are inserted into columns afterwards
			if (layoutType == LayoutType::List)
			{
				auto itNext = m_activeEntries.lower_bound(i + 1);

				Rml::Element* insertBefore = (itNext != m_activeEntries.end())
					? itNext->second  // if there is a next entry, we insert before it
					: m_spacerBR; // otherwise we insert before the bottom spacer

				InsertBefore(std::move(e), insertBefore);

				m_activeEntries.insert({ i, entry });
			}
			else
			{
				m_detachedEntries.insert({ i, std::move(e) });
			}
		}

		return dirty;
	}

	bool ElemList::updateActiveColumns(size_t _firstColumn, size_t _lastColumn, uint32_t _itemsPerColumn, bool _forceRefresh)
	{
		bool dirty = false;

		// return elements back to pool that we don't need
		for (auto it = m_activeColumns.begin(); it != m_activeColumns.end();)
		{
			const auto i = it->first;
			auto& entry = it->second;

			const auto isInRange = !_forceRefresh && i >= _firstColumn && i <= _lastColumn;

			if (!isInRange)
			{
				auto e = RemoveChild(entry);
				assert(e);

				// remove all entries from that column.
				// They have been put back into the pool already by updateActiveEntries()
				while (e->GetNumChildren())
					e->RemoveChild(e->GetChild(0));
				m_inactiveColumns.emplace_back(std::move(e));
				it = m_activeColumns.erase(it);
				dirty = true;
			}
			else
			{
				++it;
			}
		}

		// create new elements for columns that are not active yet
		const auto elemSize = updateElementSize();

		for (size_t i = _firstColumn; i <= _lastColumn; ++i)
		{
			if (m_activeColumns.find(i) != m_activeColumns.end())
				continue;

			dirty = true;

			ElementPtr e;
			if (!m_inactiveColumns.empty())
			{
				e = std::move(m_inactiveColumns.back());
				m_inactiveColumns.pop_back();
			}
			else
			{
				auto* doc = GetOwnerDocument();

				e = doc->CreateElement("div");
				e->SetProperty(PropertyId::FlexGrow, Property(0.f, Unit::NUMBER));
				e->SetProperty(PropertyId::FlexShrink, Property(0.f, Unit::NUMBER));

//				e->SetProperty("background-color", "#00f7");
//				e->SetProperty("border-radius", "20dp");
			}

			e->SetProperty(PropertyId::Display, Property(Style::Display::Block));

			helper::changeProperty(e.get(), PropertyId::FlexBasis, Property(elemSize.x, Unit::PX));
			helper::changeProperty(e.get(), PropertyId::Width, Property(elemSize.x, Unit::PX));

			auto itNext = m_activeColumns.lower_bound(i + 1);

			Rml::Element* insertBefore = itNext != m_activeColumns.end()
				? itNext->second  // if there is a next entry, we insert before it
				: m_spacerBR; // otherwise we insert before the bottom spacer

			m_activeColumns.insert({ i, e.get() });

			auto* col = InsertBefore(std::move(e), insertBefore);

			// attach items to this column
			while (col->GetNumChildren())
				col->RemoveChild(col->GetChild(0));

			const auto firstEntry = i * _itemsPerColumn;
			const auto lastEntry = std::min(firstEntry + _itemsPerColumn - 1, m_list.size() - 1);
			for (size_t j = firstEntry; j <= lastEntry; ++j)
			{
				auto it = m_detachedEntries.find(j);
				if (it == m_detachedEntries.end())
					continue;

				auto* active = col->AppendChild(std::move(it->second));
				active->SetProperty(PropertyId::Display, Property(Style::Display::Block));
//				assert(active->GetDisplay() == Style::Display::Block);
				assert(active->IsVisible());
				m_detachedEntries.erase(it);
				m_activeEntries.insert({ j, active });
			}
		}

		return dirty;
	}

	void ElemList::setSpacerTL(const float _size)
	{
		setSpacer(m_spacerTL, _size);
	}

	void ElemList::setSpacerBR(const float _size)
	{
		return setSpacer(m_spacerBR, _size);
	}

	void ElemList::setSpacer(Rml::Element* _spacer, float _size)
	{
		if (_spacer == nullptr)
			return;

		if (_size > 0)
		{
			const auto layoutType = getLayoutType();

			if (layoutType == LayoutType::Grid)
			{
				helper::changeProperty(_spacer, PropertyId::FlexBasis, Property(_size, Unit::PX));
				helper::changeProperty(_spacer, PropertyId::Width, Property(_size, Unit::PX));
			}
			else
			{
				helper::changeProperty(_spacer, PropertyId::Height, Property(_size, Unit::PX));
			}

			helper::changeProperty(_spacer, PropertyId::Display, Property(Style::Display::Block));
		}
		else
		{
			helper::changeProperty(_spacer, PropertyId::Display, Property(Style::Display::None));
		}
	}

	Rml::Element* ElemList::createSpacer()
	{
		auto* doc = GetOwnerDocument();

		auto spacer = doc->CreateElement("div");

		if (getLayoutType() == LayoutType::List)
		{
			spacer->SetProperty(PropertyId::Width, Property(100.0f, Unit::PERCENT));
			spacer->SetProperty(PropertyId::Height, Property(10.0f, Unit::DP));
			spacer->SetProperty(PropertyId::Display, Property(Style::Display::Block));
		}

		if (getLayoutType() == LayoutType::Grid)
		{
			spacer->SetProperty(PropertyId::Height, Property(100.0f, Unit::PERCENT));
			spacer->SetProperty(PropertyId::Width, Property(10.0f, Unit::PX));
			spacer->SetProperty(PropertyId::Display, Property(Style::Display::Block));
			spacer->SetProperty(PropertyId::FlexGrow, Property(0.f, Unit::NUMBER));
			spacer->SetProperty(PropertyId::FlexShrink, Property(0.f, Unit::NUMBER));
			spacer->SetProperty(PropertyId::FlexBasis, Property(10.f, Unit::PX));
//			spacer->SetProperty("flex", "0 0 auto");
		}

		spacer->SetProperty("border-color", "#f0f3");
		spacer->SetProperty("border-radius", "15dp");
//		spacer->SetProperty(PropertyId::Display, Property(Style::Display::None));

		return AppendChild(std::move(spacer));
	}

	void ElemList::onScroll(const Event&)
	{
		updateLayout();
//		m_layoutDirty = 1;
	}

	ElemList::LayoutType ElemList::getLayoutType()
	{
		auto propX = GetProperty(PropertyId::OverflowX);
		auto propY = GetProperty(PropertyId::OverflowY);

		if (propY)
		{
			const auto v = propY->Get<Style::Overflow>();

			if (v == Style::Overflow::Scroll || v == Style::Overflow::Auto)
				return LayoutType::List;
		}
		if (propX)
		{
			auto v = propX->Get<Style::Overflow>();

			if (v == Style::Overflow::Scroll || v == Style::Overflow::Auto)
				return LayoutType::Grid;
		}
		return LayoutType::None;
	}

	uint32_t ElemList::getItemsPerColumn()
	{
		const auto box = GetBox();
		const auto size = box.GetSize(Rml::BoxArea::Content);

		const auto elementSize = updateElementSize();

		if (elementSize.y <= 0)
			return 0;

		const int itemsPerColumn = static_cast<int>(size.y / elementSize.y);

		return itemsPerColumn;
	}
}
