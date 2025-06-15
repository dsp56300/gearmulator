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
		enum class LayoutType : uint8_t
		{
			None,
			List,
			Grid,
		};

		explicit ElemList(const Rml::String& _tag);

		void OnChildAdd(Rml::Element* _child) override;
		void OnUpdate() override;
		void OnDpRatioChange() override;
		void OnLayout() override;
		void OnResize() override;

		void ProcessEvent(Rml::Event& _event) override;

	private:
		void initialize();
		Rml::Vector2f updateElementSize();
		bool updateLayout();
		bool updateLayoutVertical();
		bool updateLayoutGrid();
		bool updateActiveEntries(size_t _firstEntry, size_t _lastEntry, bool _forceRefresh);
		bool updateActiveColumns(size_t _firstColumn, size_t _lastColumn, uint32_t _itemsPerColumn, bool _forceRefresh);

		void setSpacerTL(float _size);
		void setSpacerBR(float _size);
		void setSpacer(Rml::Element* _spacer, float _size);
		Rml::Element* createSpacer();

		void onScroll(const Rml::Event& _event);

		uint32_t getItemsPerColumn();

		LayoutType getLayoutType();

		Rml::Element* m_spacerTL = nullptr;
		Rml::Element* m_spacerBR = nullptr;
		ElemListEntry* m_entryTemplate = nullptr;
		Rml::ElementPtr m_entryTemplatePtr = nullptr;

		std::map<size_t, Rml::Element*> m_activeEntries;
		std::map<size_t, Rml::ElementPtr> m_detachedEntries;
		std::vector<Rml::ElementPtr> m_inactiveEntries;

		std::map<size_t, Rml::Element*> m_activeColumns;
		std::vector<Rml::ElementPtr> m_inactiveColumns;

		uint32_t m_layoutDirty = 1;
		uint32_t m_lastItemsPerColumn = 0;

		Rml::Vector2f m_elementSize{ 0,0 };

		List m_list;
	};
}
