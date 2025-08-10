#pragma once

#include "rmlDragSource.h"
#include "rmlDragTarget.h"
#include "rmlElement.h"
#include "rmlList.h"

namespace juceRmlUi
{
	class ElemListEntry : public Element, public DragSource, public DragTarget
	{
	public:
		explicit ElemListEntry(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);

		void setListItem(const List::EntryPtr& _entry);

		virtual void onEntryChanged();

		virtual void onClick(const Rml::Event& _event);

		std::unique_ptr<DragData> createDragData() override;

		auto& getEntry() const { return m_entry; }

	private:
		void onAdded();
		void onRemoved();
		void onIndexChanged();
		void onSelectedChanged(bool _selected);

		void updateState();

		List::EntryPtr m_entry;

		baseLib::EventListener<ListEntry*> m_onIndexChanged;
		baseLib::EventListener<ListEntry*, List*> m_onRemoved;
		baseLib::EventListener<ListEntry*, List*> m_onAdded;
		baseLib::EventListener<ListEntry*, bool> m_onSelected;
	};
}
