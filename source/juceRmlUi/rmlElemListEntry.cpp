#include "rmlElemListEntry.h"

#include "rmlListEntry.h"

#include "rmlEventListener.h"
#include "rmlHelper.h"

namespace juceRmlUi
{
	ElemListEntry::ElemListEntry(const Rml::String& _tag) : Element(_tag)
	{
		EventListener::Add(this, Rml::EventId::Click, [this](const Rml::Event& _event)
		{
			onClick(_event);
		});
	}

	void ElemListEntry::setListItem(const List::EntryPtr& _entry)
	{
		if (m_entry == _entry)
			return;
		if (m_entry)
			m_onSelected.reset();
		m_entry = _entry;
		if (m_entry)
			m_onSelected.set(_entry->evSelected, [this](ListEntry*, bool _selected) {  onSelectedChanged(_selected); });
		onEntryChanged();
	}

	void ElemListEntry::onEntryChanged()
	{
		SetInnerRML("Entry " + std::to_string(m_entry->getIndex()));
		updateState();
	}

	void ElemListEntry::onClick(const Rml::Event& _event)
	{
		auto& list = m_entry->getList();

		const auto ctrl = helper::getKeyModCtrl(_event);
		const auto shift = helper::getKeyModShift(_event);

		if (shift && list.getMultiselect())
		{
			if (list.selectRangeViaShiftKey(m_entry->getIndex()))
				return;
		}

		list.setSelected(m_entry->getIndex(), true, ctrl);
	}

	void ElemListEntry::onSelectedChanged(const bool _selected)
	{
		updateState();
	}

	void ElemListEntry::updateState()
	{
		const auto selected = m_entry && m_entry->isSelected();
		SetPseudoClass("selected", selected);
	}
}
