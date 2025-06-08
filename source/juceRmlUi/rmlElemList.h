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
		explicit ElemList(const Rml::String& _tag);

		void OnChildAdd(Rml::Element* _child) override;
		void OnUpdate() override;
		void OnDpRatioChange() override;

		void ProcessEvent(Rml::Event& _event) override;

	private:
		void initialize();
		Rml::Vector2f updateElementSize();
		bool updateLayout();
		void updateActiveEntries(size_t _firstEntry, size_t _lastEntry);

		void onScroll(const Rml::Event& _event);

		Rml::Element* m_spacerTL = nullptr;
		Rml::Element* m_spacerBR = nullptr;
		ElemListEntry* m_entryTemplate = nullptr;

		std::map<size_t, Rml::Element*> m_activeEntries;
		std::vector<Rml::ElementPtr> m_inactiveEntries;

		uint32_t m_layoutDirty = 1;

		Rml::Vector2f m_elementSize{ 0,0 };

		List m_list;
	};
}
