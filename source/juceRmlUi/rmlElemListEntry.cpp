#include "rmlElemListEntry.h"

#include "rmlListEntry.h"

namespace juceRmlUi
{
	ElemListEntry::ElemListEntry(const Rml::String& _tag) : Element(_tag)
	{
	}

	void ElemListEntry::setListItem(const List::EntryPtr& _entry)
	{
		if (m_entry == _entry)
			return;
		m_entry = _entry;
		onEntryChanged();
	}

	void ElemListEntry::onEntryChanged()
	{
		SetInnerRML("Entry " + std::to_string(m_entry->getIndex()));
	}
}
