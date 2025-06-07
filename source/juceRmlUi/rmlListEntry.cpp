#include "rmlListEntry.h"

namespace juceRmlUi
{
	ListEntry::ListEntry(List& _list) : m_list(_list)
	{
	}

	void ListEntry::setSelected(const bool _selected)
	{
		if (_selected == m_selected)
			return;
		evSelected(this, _selected);
		m_selected = _selected;
	}

	void ListEntry::setIndex(const size_t _index)
	{
		if (_index == m_index)
			return;

		const auto oldIndex = m_index;
		m_index = _index;

		if (oldIndex == InvalidIndex)
			evAdded(this, &m_list);

		if (_index != InvalidIndex)
			evIndexChanged(this);
		else
			evRemoved(this, &m_list);
	}
}
