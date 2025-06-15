#pragma once

#include "baseLib/event.h"

namespace juceRmlUi
{
	class List;

	class ListEntry
	{
		friend class List;

	public:
		static constexpr size_t InvalidIndex = static_cast<size_t>(-1);

		baseLib::Event<ListEntry*> evIndexChanged;
		baseLib::Event<ListEntry*, List*> evRemoved;
		baseLib::Event<ListEntry*, List*> evAdded;
		baseLib::Event<ListEntry*, bool> evSelected;

		explicit ListEntry(List& _list);
		ListEntry(const ListEntry&) = delete;
		ListEntry(ListEntry&&)  noexcept = default;
		ListEntry& operator=(const ListEntry&) = delete;
		ListEntry& operator=(ListEntry&&) = delete;

		virtual ~ListEntry() = default;

		bool isSelected() const { return m_selected; }

		size_t getIndex() const { return m_index; }

		bool isValid() const { return m_index != InvalidIndex; }

		List& getList() const { return m_list; }

	private:
		void setSelected(bool _selected);

		void setIndex(size_t _index);

		List& m_list;
		size_t m_index = InvalidIndex;

		bool m_selected = false;
	};
}
