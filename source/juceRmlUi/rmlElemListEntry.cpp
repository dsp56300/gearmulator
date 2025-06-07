#include "rmlElemListEntry.h"

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
}
