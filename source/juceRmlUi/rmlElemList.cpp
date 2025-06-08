#include "rmlElemList.h"

#include "rmlElemListEntry.h"
#include "rmlListEntry.h"

namespace juceRmlUi
{
	ElemList::ElemList(const Rml::String& _tag) : Element(_tag)
	{
		for (size_t i=0; i<100; ++i)
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

	void ElemList::initialize()
	{
		if (GetNumChildren() < 3)
			return;

		m_spacerTL = GetChild(0);
		m_spacerBR = GetChild(GetNumChildren() - 1);

		const auto entryTemplate = GetChild(1);
		m_entryTemplate = dynamic_cast<ElemListEntry*>(entryTemplate);

		if (m_entryTemplate == nullptr)
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "list item template needs to be of type 'listitem', got '%s' instead", entryTemplate->GetTagName().c_str());
			return;
		}

		AddEventListener(Rml::EventId::Scroll, this);
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

	bool ElemList::updateLayout()
	{
		const auto box = GetBox();
		const auto size = box.GetSize(Rml::BoxArea::Content);
		const auto scrollTop = GetScrollTop();

		const auto elementHeight = updateElementSize().y;
		
		if (elementHeight <= 0.0f)
			return false;

		const auto firstEntry = static_cast<int>(scrollTop / elementHeight);
		const auto lastEntry = static_cast<int>(std::ceil((scrollTop + size.y) / elementHeight));

		updateActiveEntries(firstEntry, lastEntry);

		if (firstEntry > 0)
		{
			m_spacerTL->SetProperty(Rml::PropertyId::Height, Rml::Property(static_cast<float>(firstEntry) * elementHeight, Rml::Unit::PX));
			m_spacerTL->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::Block));
		}
		else
		{
			m_spacerTL->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
		}

		if (static_cast<size_t>(lastEntry) < m_list.size())
		{
			m_spacerBR->SetProperty(Rml::PropertyId::Height, Rml::Property(static_cast<float>(m_list.size() - lastEntry) * elementHeight, Rml::Unit::PX));
			m_spacerBR->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::Block));
		}
		else
		{
			m_spacerBR->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
		}

		return true;
	}

	void ElemList::updateActiveEntries(const size_t _firstEntry, size_t _lastEntry)
	{
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

				e->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::Block));
			}

			auto entry = dynamic_cast<ElemListEntry*>(e.get());
			if (entry)
				entry->setListItem(m_list.getEntry(i));

			auto itNext = m_activeEntries.lower_bound(i + 1);

			Rml::Element* insertBefore = (itNext != m_activeEntries.end())
				? itNext->second  // if there is a next entry, we insert before it
				: m_spacerBR; // otherwise we insert before the bottom spacer

			m_activeEntries.insert({ i, entry });

			InsertBefore(std::move(e), insertBefore);
		}
	}

	void ElemList::onScroll(const Rml::Event& _event)
	{
		m_layoutDirty = 1;
	}
}
