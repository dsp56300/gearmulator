#pragma once

#include "RmlUi/Core/EventListener.h"
#include "RmlUi/Core/Types.h"
#include "RmlUi/Core/DataModelHandle.h"

#include <limits>

#include "baseLib/event.h"

namespace juceRmlUi
{
	class ElemButton;
}

namespace rmlPlugin
{
	class TabGroup : Rml::EventListener
	{
	public:
		TabGroup(std::string _name, Rml::Context* _context);
		TabGroup(const TabGroup&) = delete;
		TabGroup(TabGroup&&) = delete;

		~TabGroup() override;

		void setPage(Rml::Element* _page, size_t _index);
		void setButton(Rml::Element* _button, size_t _index);

		void ProcessEvent(Rml::Event& _event) override;

		void setActivePage(size_t _index);

		TabGroup& operator=(const TabGroup&) = delete;
		TabGroup& operator=(TabGroup&&) = delete;

		static bool isChecked(const Rml::Element* _button);
		static void setChecked(Rml::Element* _button, bool _checked);
		static bool isToggle(const Rml::Element* _button);

		bool selectTabWithElement(const Rml::Element* _element);

	private:
		void resize(size_t _size);

		void onClick(size_t _index);

		void setPageActive(size_t _index, bool _active) const;

		std::string m_name;
		Rml::DataModelHandle m_dataModel;

		std::vector<std::vector<Rml::Element*>> m_buttons;
		std::vector<std::vector<baseLib::EventListener<juceRmlUi::ElemButton*>>> m_buttonListeners;
		std::vector<Rml::Element*> m_pages;

		uint32_t m_activePage = 0;
	};
}
