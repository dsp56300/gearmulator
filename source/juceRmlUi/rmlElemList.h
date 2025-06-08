#pragma once

#include "rmlElement.h"
#include "rmlList.h"
#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class ElemListEntry;

	class ElemList : public Element, public List, Rml::EventListener
	{
	public:
		enum LayoutType : uint8_t
		{
			None,
			VerticalList,
			GridLayout,
		};

		explicit ElemList(const Rml::String& _tag);

		void OnChildAdd(Rml::Element* _child) override;
		void OnUpdate() override;
		void ProcessEvent(Rml::Event& _event) override;
		void OnLayout() override;
		void OnResize() override;
		void OnDpRatioChange() override;
		void OnPropertyChange(const Rml::PropertyIdSet& changed_properties) override;

	private:
		void initialize();
		Rml::Vector2f updateElementSize();
		bool updateActiveEntries();
		bool updateActiveEntriesX();
		bool updateActiveEntriesY();
		bool updateActiveEntries(size_t _firstEntry, size_t _lastEntry);
		void updateActiveEntriesLayoutGrid(size_t _itemsPerColumn);
		void updateActiveEntriesLayoutVertical();
		uint32_t getItemsPerColumn();
		void setSpacerSize(Rml::Vector2f size) const;
		void updateSpacerSize();

		LayoutType getLayoutType();

		void onScroll(const Rml::Event& _event);

		Rml::Element* m_spacer = nullptr;
		ElemListEntry* m_entryTemplate = nullptr;

		std::map<size_t, Rml::Element*> m_activeEntries;
		std::vector<Rml::ElementPtr> m_inactiveEntries;

		bool m_activeEntriesDirty = true;
		bool m_spacerDirty = true;

		Rml::Vector2f m_elementSize{ 0,0 };

		List m_list;
	};
}
