#include "rmlElemList.h"

#include "rmlElemListEntry.h"
#include "rmlListEntry.h"

#include "RmlUi/Core/ElementDocument.h"

namespace juceRmlUi
{
	ElemList::ElemList(const Rml::String& _tag) : Element(_tag)
	{
		for (size_t i=0; i<300; ++i)
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

		if (m_spacer == nullptr)
			initialize();

		if (m_activeEntriesDirty)
		{
			if (m_spacer != nullptr)
			{
				updateActiveEntries();
				m_activeEntriesDirty = false;
			}
		}
		if (m_spacerDirty)
		{
			updateSpacerSize();
			m_spacerDirty = false;
		}
	}

	void ElemList::ProcessEvent(Rml::Event& _event)
	{
		switch (_event.GetId())
		{
		case Rml::EventId::Scroll:
			onScroll(_event);
			break;
		default:;
		}
	}

	void ElemList::OnLayout()
	{
		Element::OnLayout();

		const auto layoutType = getLayoutType();

		if (layoutType == None)
			return;

		if (layoutType == VerticalList)
		{
			updateActiveEntriesLayoutVertical();
		}
		else if (layoutType == GridLayout)
		{
			const auto itemsPerColumn = getItemsPerColumn();
			if (!itemsPerColumn)
				return;
			updateActiveEntriesLayoutGrid(itemsPerColumn);
		}
	}

	void ElemList::OnResize()
	{
		Element::OnResize();
		m_spacerDirty = true;
	}

	void ElemList::OnDpRatioChange()
	{
		Element::OnDpRatioChange();
		m_activeEntriesDirty = true;
	}

	void ElemList::OnPropertyChange(const Rml::PropertyIdSet& changed_properties)
	{
		Element::OnPropertyChange(changed_properties);
	}

	void ElemList::initialize()
	{
		if (!GetNumChildren())
			return;

		for (int i=0; i<GetNumChildren(); ++i)
		{
			auto e = GetChild(i);
			m_entryTemplate = dynamic_cast<ElemListEntry*>(e);
			if (m_entryTemplate)
				break;
		}

		if (m_entryTemplate == nullptr)
			Rml::Log::Message(Rml::Log::LT_ERROR, "list item needs to be child of type 'listitem'");

		auto spacer = GetOwnerDocument()->CreateElement("div");

		spacer->SetProperty("display", "block");
//		spacer->SetProperty("position", "static");
//		spacer->SetProperty("visibility", "hidden");
		spacer->SetProperty("background-color", "#f0f5");
		spacer->SetProperty("pointer-events", "none");
		spacer->SetProperty("border-radius", "20dp");
		if (getLayoutType() == GridLayout)
			spacer->SetProperty(Rml::PropertyId::Height, Rml::Property(100, Rml::Unit::PERCENT));
		if (getLayoutType() == VerticalList)
			spacer->SetProperty(Rml::PropertyId::Width, Rml::Property(100, Rml::Unit::PERCENT));

		m_spacer = AppendChild(std::move(spacer));

		AddEventListener(Rml::EventId::Scroll, this);

		m_activeEntriesDirty = true;
	}

	Rml::Vector2f ElemList::updateElementSize()
	{
		if (!m_activeEntries.empty())
		{
			auto size = m_activeEntries.begin()->second->GetBox().GetSize(Rml::BoxArea::Margin);

			if (size.x > 0 && size.y > 0)
			{
				m_elementSize = size;
				return size;
			}
		}
		if (m_elementSize.x <= 0.0f || m_elementSize.y <= 0 && m_entryTemplate->GetDisplay() != Rml::Style::Display::None)
		{
			m_elementSize = m_entryTemplate->GetBox().GetSize(Rml::BoxArea::Margin);
			if (m_elementSize.x > 0 && m_elementSize.y > 0)
			{
				m_entryTemplate->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
				return m_elementSize;
			}
		}
		return m_elementSize;
	}

	bool ElemList::updateActiveEntries()
	{
		if (m_spacer == nullptr)
			return false;

		const auto layoutType = getLayoutType();
		if (layoutType == VerticalList)
		{
			if (updateActiveEntriesY())
			{
				DirtyLayout();
			}
			m_spacerDirty = true;
			return true;
		}
		if (layoutType == GridLayout)
		{
			if (updateActiveEntriesX())
			{
				DirtyLayout();
			}
			m_spacerDirty = true;
			return true;
		}
		return false;
	}

	bool ElemList::updateActiveEntriesX()
	{
		auto box = GetBox();
		const auto size = box.GetSize(Rml::BoxArea::Padding);
		const auto scrollLeft = GetScrollLeft();

		const auto elementSize = updateElementSize();

		if (elementSize.x <= 0 || elementSize.y <= 0)
			return false;

		const auto itemsPerColumn = getItemsPerColumn();

		if (!itemsPerColumn)
			return false;

		const auto firstColumn = static_cast<int>(scrollLeft / elementSize.x);
		const auto lastColumn = static_cast<int>(std::ceil((scrollLeft + size.x) / elementSize.x));

		const auto firstEntry = firstColumn * itemsPerColumn;
		const auto lastEntry = lastColumn * itemsPerColumn - 1;

		return updateActiveEntries(firstEntry, lastEntry);
	}

	bool ElemList::updateActiveEntriesY()
	{
		const auto box = GetBox();
		const auto size = GetScrollHeight();//box.GetSize(Rml::BoxArea::Padding);
		const auto scrollTop = GetScrollTop();

		const auto elementHeight = updateElementSize().y;
		
		if (elementHeight <= 0.0f)
			return false;

		const auto firstEntry = static_cast<int>(scrollTop / elementHeight);
		const auto lastEntry = static_cast<int>(std::ceil((scrollTop + size) / elementHeight));

		return updateActiveEntries(firstEntry, lastEntry);
	}

	bool ElemList::updateActiveEntries(const size_t _firstEntry, size_t _lastEntry)
	{
		bool dirty = false;

		// return elements back to pool that we don't need
		for (auto it = m_activeEntries.begin(); it != m_activeEntries.end();)
		{
			const auto i = it->first;
			auto& entry = it->second;

			const auto isInRange = i >= _firstEntry && i <= _lastEntry;

			if (!isInRange)
			{
				auto e = RemoveChild(entry);
				assert(e);
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

			Rml::ElementPtr e;
			if (!m_inactiveEntries.empty())
			{
				e = std::move(m_inactiveEntries.back());
				m_inactiveEntries.pop_back();
			}
			else
			{
				e = m_entryTemplate->Clone();

				e->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::Inline));
			}

			e->SetProperty(Rml::PropertyId::Position, Rml::Property(Rml::Style::Position::Absolute));

			auto entry = dynamic_cast<ElemListEntry*>(e.get());
			if (entry)
				entry->setListItem(m_list.getEntry(i));

			auto itNext = m_activeEntries.lower_bound(i + 1);

			Rml::Element* insertBefore = itNext != m_activeEntries.end()
				? itNext->second  // if there is a next entry, we insert before it
				: nullptr; // otherwise we insert before the spacer

			m_activeEntries.insert({ i, entry });

			if (insertBefore != nullptr)
				InsertBefore(std::move(e), insertBefore);
			else
				InsertBefore(std::move(e), m_spacer);

			dirty = true;
		}
		return dirty;
	}

	void ElemList::updateActiveEntriesLayoutGrid(size_t _itemsPerColumn)
	{
		const auto offset = GetBox().GetPosition(Rml::BoxArea::Padding);

		for (auto& [idx,e] : m_activeEntries)
		{
			const auto column = idx / _itemsPerColumn;
			const auto row = idx - column * _itemsPerColumn;

			const auto x = static_cast<float>(column) * m_elementSize.x;
			const auto y = static_cast<float>(row) * m_elementSize.y;

			e->SetOffset(Rml::Vector2f(x, y) + offset, this, false);
		}
	}
	void ElemList::updateActiveEntriesLayoutVertical()
	{
		const auto offset = GetBox().GetPosition(Rml::BoxArea::Padding);
		for (auto& [idx, e] : m_activeEntries)
		{
			const auto y = static_cast<float>(idx) * m_elementSize.y;
			e->SetOffset(Rml::Vector2f(offset.x, y) + offset, this, false);
		}
	}

	uint32_t ElemList::getItemsPerColumn()
	{
		const auto box = GetBox();
		const auto size = box.GetSize(Rml::BoxArea::Padding);

		const auto elementSize = updateElementSize();

		if (elementSize.y <= 0)
			return 0;

		const int itemsPerColumn = static_cast<int>(size.y / elementSize.y);

		return itemsPerColumn;
	}

	void ElemList::setSpacerSize(const Rml::Vector2f _size) const
	{
		const auto spacerBox = m_spacer->GetBox();
		const auto spacerSize = spacerBox.GetSize(Rml::BoxArea::Content);

		if (_size.x > 0 && std::fabs(_size.x - spacerSize.x) > 0.1f)
		{
			m_spacer->SetProperty(Rml::PropertyId::Width, Rml::Property(_size.x, Rml::Unit::PX));
		}
		if (_size.y > 0 && std::abs(_size.y - spacerSize.y) > 0.1f)
		{
			m_spacer->SetProperty(Rml::PropertyId::Height, Rml::Property(_size.y, Rml::Unit::PX));
		}
	}

	void ElemList::updateSpacerSize()
	{
		const auto layoutType = getLayoutType();

		if (layoutType == None)
			return;

		if (layoutType == VerticalList)
		{
			const auto elementHeight = updateElementSize().y;
			setSpacerSize(Rml::Vector2f(0, elementHeight * static_cast<float>(m_list.size())));
		}

		else if (layoutType == GridLayout)
		{
			const auto itemsPerColumn = getItemsPerColumn();
			if (!itemsPerColumn)
				return;

			const auto elementSize = updateElementSize();

			const auto sizeX = static_cast<float>(std::floor(m_list.size() / itemsPerColumn)) * elementSize.x;

			setSpacerSize(Rml::Vector2f(sizeX, 0));
		}
	}

	ElemList::LayoutType ElemList::getLayoutType()
	{
		auto propX = GetProperty(Rml::PropertyId::OverflowX);
		auto propY = GetProperty(Rml::PropertyId::OverflowY);

		if (propY)
		{
			const auto v = propY->Get<Rml::Style::Overflow>();

			if (v == Rml::Style::Overflow::Scroll || v == Rml::Style::Overflow::Auto)
				return VerticalList;
		}
		if (propX)
		{
			auto v = propX->Get<Rml::Style::Overflow>();

			if (v == Rml::Style::Overflow::Scroll || v == Rml::Style::Overflow::Auto)
				return GridLayout;
		}
		return None;
	}

	void ElemList::onScroll(const Rml::Event&)
	{
		updateActiveEntries();
	}
}
