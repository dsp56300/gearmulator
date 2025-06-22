#include "rmlElemListEntry.h"

#include "rmlDragData.h"
#include "rmlDragSource.h"
#include "rmlDragTarget.h"
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

		DragSource::init(this);
		DragTarget::init(this);
	}

	void ElemListEntry::setListItem(const List::EntryPtr& _entry)
	{
		if (m_entry == _entry)
			return;
		if (m_entry)
		{
			m_onAdded.reset();
			m_onRemoved.reset();
			m_onIndexChanged.reset();
			m_onSelected.reset();

			m_entry->setElement(nullptr);
		}

		m_entry = _entry;

		if (m_entry)
		{
			m_onAdded.set(_entry->evAdded, [this](ListEntry*, List*) {  onAdded(); });
			m_onRemoved.set(_entry->evRemoved, [this](ListEntry*, List*) {  onRemoved(); });
			m_onIndexChanged.set(_entry->evIndexChanged, [this](ListEntry*) {  onIndexChanged(); });
			m_onSelected.set(_entry->evSelected, [this](ListEntry*, bool _selected) {  onSelectedChanged(_selected); });

			m_entry->setElement(this);
		}
		onEntryChanged();
		updateState();
	}

	void ElemListEntry::onEntryChanged()
	{
		SetInnerRML("Entry " + std::to_string(m_entry->getIndex()));
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

	std::unique_ptr<DragData> ElemListEntry::createDragData()
	{
		return std::make_unique<DragData>();
	}

	void ElemListEntry::onAdded()
	{
	}

	void ElemListEntry::onRemoved()
	{
	}

	void ElemListEntry::onIndexChanged()
	{
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
