#pragma once

#include <memory>
#include <vector>

#include "baseLib/event.h"

namespace Rml::Input
{
	enum KeyIdentifier : unsigned char;
}

namespace juceRmlUi
{
	class ListEntry;

	class List
	{
	public:
		static constexpr size_t InvalidIndex = static_cast<size_t>(-1);
		using EntryPtr = std::shared_ptr<ListEntry>;

		baseLib::Event<List*, EntryPtr> evEntryAdded;
		baseLib::Event<List*, EntryPtr> evEntryRemoved;
		baseLib::Event<List*> evEntriesMoved;

		List();
		List(const List&) = delete;
		List(List&&) noexcept = default;
		List& operator=(const List&) = delete;
		List& operator=(List&&) noexcept = default;

		virtual ~List() = default;

		bool empty() const { return m_entries.empty(); }
		size_t size() const { return m_entries.size(); }

		void addEntry(EntryPtr&& _entry);

		bool removeEntry(const EntryPtr& _entry);
		bool removeEntry(size_t _index);
		bool removeEntries(std::vector<size_t> _indices);

		EntryPtr getEntry(size_t _index) const;

		bool setSelected(size_t _index, bool _selected, bool _allowMultiselect = true) const;

		bool moveEntryTo(size_t _oldIndex, size_t _newIndex);
		bool moveEntriesTo(const std::vector<size_t>& _entries, size_t _newIndex);

		std::vector<size_t> getSelectedIndices() const;
		std::vector<EntryPtr> getSelectedEntries() const;

		std::vector<EntryPtr> getEntries() const { return m_entries; }

		bool getMultiselect() const { return m_allowMultiselect; }
		void setMultiselect(const bool _enabled) { m_allowMultiselect = _enabled; }

		bool handleNavigationKey(Rml::Input::KeyIdentifier _key, bool _ctrl, bool _shift, uint32_t _gridItemsPerColumn);
		bool selectRangeViaShiftKey(size_t _index) const;

	private:
		bool moveEntryTo(const ListEntry& _e, size_t _newIndex);

		void updateIndices(size_t _first, size_t _last) const;

		std::vector<std::shared_ptr<ListEntry>> m_entries;

		bool m_allowMultiselect = false;
	};
}
