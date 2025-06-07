#pragma once

#include "rmlElement.h"
#include "rmlList.h"

namespace juceRmlUi
{
	class ElemListEntry : public Element
	{
	public:
		explicit ElemListEntry(const Rml::String& _tag);

		void setListItem(const List::EntryPtr& _entry);

		virtual void onEntryChanged();

	private:
		List::EntryPtr m_entry;
	};
}
