#include "rmlList.h"

#include <algorithm>
#include <set>

#include "rmlListEntry.h"

#include "RmlUi/Core/Input.h"

namespace juceRmlUi
{
	List::List()
	= default;

	void List::addEntry(std::shared_ptr<ListEntry>&& _entry)
	{
		_entry->setIndex(m_entries.size());
		evEntryAdded(this, _entry);
		m_entries.push_back(std::move(_entry));
	}

	bool List::removeEntry(const EntryPtr& _entry)
	{
		return removeEntry(_entry->getIndex());
	}

	bool List::removeEntry(const size_t _index)
	{
		if (_index >= m_entries.size())
			return false;

		auto& e = m_entries[_index];

		evEntryRemoved(this, e);

		e->setIndex(InvalidIndex);

		m_entries.erase(m_entries.begin() + static_cast<decltype(m_entries)::difference_type>(_index));

		updateIndices(_index, m_entries.size());

		return true;
	}

	bool List::removeEntries(std::vector<size_t> _indices)
	{
		if (_indices.empty())
			return false;

		if (_indices.size() == 1)
			return removeEntry(_indices.front());

		std::sort(_indices.begin(), _indices.end());

		const size_t minIndex = _indices.front();

		bool res = false;
		for (auto it = _indices.rbegin(); it != _indices.rend(); ++it)
		{
			const auto index = *it;
			if (index >= m_entries.size())
				continue;
			res = true;
			evEntryRemoved(this, m_entries[index]);
			m_entries[index]->setIndex(InvalidIndex);
			m_entries.erase(m_entries.begin() + static_cast<decltype(m_entries)::difference_type>(index));
		}
		if (!res)
			return false;
		updateIndices(minIndex, m_entries.size());
		return res;
	}

	List::EntryPtr List::getEntry(const size_t _index) const
	{
		return _index < m_entries.size() ? m_entries[_index] : nullptr;
	}

	bool List::setSelected(const size_t _index, const bool _selected, const bool _allowMultiselect/* = true*/, const bool _notify/* = true*/)
	{
		if (_index >= m_entries.size())
			return false;

		const auto allowMultiselect = _allowMultiselect && m_allowMultiselect;

		bool changed = false;

		if (_selected && !allowMultiselect)
		{
			for (auto& entry : m_entries)
				changed |= entry->setSelected(entry->getIndex() == _index);
		}
		else
		{
			changed = m_entries[_index]->setSelected(_selected);
		}

		if (changed && _notify)
			evSelectionChanged(this);

		return changed;
	}

	size_t List::handleNavigationKey(const Rml::Input::KeyIdentifier _key, bool _ctrl, bool _shift, const uint32_t _gridItemsPerColumn)
	{
		using namespace Rml::Input;

		switch (_key)
		{
			case KI_UP:
				{
					const auto entries = getSelectedEntries();
					if (entries.empty())
						return InvalidIndex;
					auto& e = entries.front();
					if (e->getIndex() <= 0)
						return InvalidIndex;
					size_t newIndex = e->getIndex() - 1;
					setSelected(newIndex, true, _ctrl || _shift);
					return newIndex;
				}
			case KI_DOWN:
				{
					const auto entries = getSelectedEntries();
					if (entries.empty())
						return InvalidIndex;
					auto& e = entries.back();
					if (e->getIndex() >= size() - 1)
						return InvalidIndex;
					size_t newIndex = e->getIndex() + 1;
					setSelected(newIndex, true, _ctrl || _shift);
					return newIndex;
				}
			case KI_LEFT:
				if (_gridItemsPerColumn > 1)
				{
					const auto entries = getSelectedEntries();
					if (entries.empty())
						return InvalidIndex;
					auto& e = entries.front();
					const auto currentIndex = e->getIndex();
					if (currentIndex < _gridItemsPerColumn)
						return InvalidIndex;
					const auto newIndex = currentIndex - _gridItemsPerColumn;
					if (_shift)
						selectRangeViaShiftKey(newIndex);
					else
						setSelected(newIndex, true, false);
					return newIndex;
				}
				return InvalidIndex;
			case KI_RIGHT:
				if (_gridItemsPerColumn > 1)
				{
					const auto entries = getSelectedEntries();
					if (entries.empty())
						return InvalidIndex;
					auto& e = entries.back();
					const auto currentIndex = e->getIndex();
					const auto newIndex = currentIndex + _gridItemsPerColumn;
					if (newIndex >= size())
						return InvalidIndex;
					if (_shift)
						selectRangeViaShiftKey(newIndex);
					else
						setSelected(newIndex, true, false);
					return newIndex;
				}
				return InvalidIndex;
		default:
			return InvalidIndex;
		}
	}

	bool List::selectRangeViaShiftKey(const size_t _index)
	{
		const auto selected = getSelectedEntries();

		if (selected.empty())
			return false;

		const auto firstSelected = selected.front()->getIndex();
		const auto lastSelected = selected.back()->getIndex();

		bool changed = false;

		if (_index < firstSelected)
		{
			for (size_t i=_index; i<firstSelected; ++i)
				changed |= setSelected(i, true, true, false);
		}
		else if (_index > lastSelected)
		{
			for (size_t i=lastSelected+1; i<=_index; ++i)
				changed |= setSelected(i, true, true, false);
		}

		if (changed)
			evSelectionChanged(this);

		return true;	// key was handled
	}

	bool List::setSelectedIndices(const std::vector<size_t>& _indices, const bool _notify/* = true*/)
	{
		if (_indices.empty())
			return deselectAll(_notify);

		bool res = false;

		if (_indices.size() == 1)
		{
			const auto index = _indices.front();
			for (auto & entry : m_entries)
				res |= entry->setSelected(index == entry->getIndex());
			return res;
		}

		std::set selection(_indices.begin(), _indices.end());

		for (auto & entry : m_entries)
		{
			const auto i = entry->getIndex();
			res |= entry->setSelected(selection.find(i) != selection.end());
		}

		if (res && _notify)
			evSelectionChanged(this);

		return res;
	}

	bool List::deselectAll(const bool _notify/* = true*/)
	{
		bool res = false;

		for (auto& entry : m_entries)
			res |= entry->setSelected(false);

		if (res && _notify)
			evSelectionChanged(this);

		return res;
	}

	bool List::moveEntryTo(const ListEntry& _e, const size_t _newIndex)
	{
		return moveEntryTo(_e.getIndex(), _newIndex);
	}

	bool List::moveEntryTo(const size_t _oldIndex, const size_t _newIndex)
	{
		if (_oldIndex == _newIndex)
			return true;

		if (_newIndex >= m_entries.size())
			return false;

		auto entry = m_entries[_oldIndex];

		m_entries.erase(m_entries.begin() + static_cast<decltype(m_entries)::difference_type>(_oldIndex));
		m_entries.insert(m_entries.begin() + static_cast<decltype(m_entries)::difference_type>(_newIndex), entry);

		updateIndices(std::min(_oldIndex, _newIndex), std::max(_oldIndex, _newIndex) + 1);

		evEntriesMoved(this);

		return true;
	}

	bool List::moveEntriesTo(const std::vector<size_t>& _entries, const size_t _newIndex)
	{
		if (_newIndex >= m_entries.size())
			return false;

		std::vector<EntryPtr> entriesToMove;
		entriesToMove.reserve(_entries.size());

		for (const auto& index : _entries)
		{
			if (index >= m_entries.size())
				continue;
			entriesToMove.push_back(m_entries[index]);
		}

		if (entriesToMove.empty())
			return false;

		if (entriesToMove.size() == 1)
			return moveEntryTo(entriesToMove.front()->getIndex(), _newIndex);

		size_t targetIndex = _newIndex;

		size_t minIndex = _newIndex;
		size_t maxIndex = _newIndex;

		for (const auto i : _entries)
		{
			m_entries.erase(m_entries.begin() + static_cast<decltype(m_entries)::difference_type>(i));
			if (i < targetIndex)
				--targetIndex;
			minIndex = std::min(minIndex, i);
			maxIndex = std::max(maxIndex, i);
		}

		minIndex = std::min(minIndex, targetIndex);
		maxIndex = std::max(maxIndex, targetIndex);

		for (const auto& entry : entriesToMove)
		{
			m_entries.insert(m_entries.begin() + static_cast<decltype(m_entries)::difference_type>(targetIndex), entry);
			entry->setIndex(targetIndex++);
		}

		updateIndices(minIndex, maxIndex + 1);

		evEntriesMoved(this);

		return true;
	}

	std::vector<size_t> List::getSelectedIndices() const
	{
		const auto entries = getSelectedEntries();
		std::vector<size_t> selectedIndices;
		selectedIndices.reserve(entries.size());
		for (const auto& entry : entries)
			selectedIndices.push_back(entry->getIndex());
		return selectedIndices;
	}

	std::vector<List::EntryPtr> List::getSelectedEntries() const
	{
		std::vector<EntryPtr> selectedEntries;
		for (const auto& entry : m_entries)
		{
			if (entry->isSelected())
				selectedEntries.push_back(entry);
		}
		return selectedEntries;
	}

	void List::updateIndices(const size_t _first, const size_t _last) const
	{
		for (size_t i = _first; i < _last; ++i)
			m_entries[i]->setIndex(i);
	}
} // namespace juceRmlUi
