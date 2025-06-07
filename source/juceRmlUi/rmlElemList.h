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

		void ProcessEvent(Rml::Event& _event) override;

	private:
		void initialize();
		float updateElementHeight();
		bool updateLayout();
		void updateActiveEntries(size_t _firstEntry, size_t _lastEntry);

		void onScroll(const Rml::Event& _event);

		Rml::Element* m_spacerTop = nullptr;
		Rml::Element* m_spacerBottom = nullptr;
		ElemListEntry* m_entryTemplate = nullptr;

		std::map<size_t, Rml::Element*> m_activeEntries;
		std::vector<Rml::ElementPtr> m_inactiveEntries;

		bool m_layoutDirty = true;

		float m_elementHeight = 0.0f;

		List m_list;
	};
}
