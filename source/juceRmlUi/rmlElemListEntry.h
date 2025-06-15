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

		void onClick(const Rml::Event& _event);

	private:
		void onSelectedChanged(bool _selected);

		void updateState();

		List::EntryPtr m_entry;

		baseLib::EventListener<ListEntry*, bool> m_onSelected;
	};
}
