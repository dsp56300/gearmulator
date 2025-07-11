#pragma once

#include <functional>
#include <string>

#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/EventListener.h"

namespace Rml
{
	class Element;
}

namespace juceRmlUi
{
	class Menu : Rml::EventListener
	{
	public:
		Menu() = default;
		Menu(Menu&&) noexcept = default;
		Menu(const Menu&) = delete;

		~Menu() override;

		Menu& operator = (Menu&&) noexcept = default;
		Menu& operator = (const Menu&) = delete;

		void addEntry(const std::string& _name, std::function<void()> _action);
		void addEntry(const std::string& _name, bool _checked, std::function<void()> _action);
		void addSeparator();
		void addSubMenu(const std::string& _name, const std::shared_ptr<Menu>& _subMenu);

		void clear()
		{
			m_entries.clear();
		}

		void open(const Rml::Element* _parent, const Rml::Vector2f& _position, uint32_t _itemsPerColumn = 16);
		void close();

		void ProcessEvent(Rml::Event& _event) override;

		void runModal(const Rml::Element* _parent, const Rml::Vector2f& _position, uint32_t _itemsPerColumn = 16);

	private:
		void closeConfirm();

		void setParentMenu(Menu* _menu);
		void openSubmenu(const Rml::ObserverPtr<Rml::Element>& _parentEntry, const std::shared_ptr<Menu>& _submenu);
		void closeSubmenu();

		struct Entry
		{
			std::string name;
			bool checked = false;
			bool separator = false;
			bool enabled = true;
			std::function<void()> action;
			std::shared_ptr<Menu> submenu;
		};

		std::vector<Entry> m_entries;
		Rml::Element* m_root = nullptr;
		Rml::ElementDocument* m_document = nullptr;

		std::shared_ptr<Menu> m_subMenu;
		Rml::ObserverPtr<Rml::Element> m_subMenuParentEntry = nullptr;
		Menu* m_parentMenu = nullptr;
	};
}
