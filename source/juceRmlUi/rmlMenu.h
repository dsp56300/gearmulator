#pragma once

#include <functional>
#include <string>

#include "rmlEventListener.h"

#include "RmlUi/Core/EventListener.h"

namespace Rml
{
	class ElementDocument;
	class Element;
}

namespace juceRmlUi
{
	class DelayedCall;

	class Menu : Rml::EventListener,  public std::enable_shared_from_this<Menu>
	{
	public:
		static constexpr uint32_t UnknownItemsPerColumn = std::numeric_limits<uint32_t>::max();

		Menu() = default;
		Menu(Menu&&) noexcept = default;
		Menu(const Menu&) = delete;

		~Menu() override;

		Menu& operator = (Menu&&) noexcept = default;
		Menu& operator = (const Menu&) = delete;

		void addEntry(const std::string& _name, std::function<void()> _action);
		void addEntry(const std::string& _name, bool _checked, std::function<void()> _action);
		void addEntry(const std::string& _name, bool _enabled, bool _checked, std::function<void()> _action);
		void addSeparator();
		void addSubMenu(const std::string& _name, const std::shared_ptr<Menu>& _subMenu);
		void addSubMenu(const std::string& _name, Menu&& _subMenu)
		{
			addSubMenu(_name, std::make_shared<Menu>(std::move(_subMenu)));
		}

		void clear()
		{
			m_entries.clear();
		}

		bool empty() const { return m_entries.empty(); }

		void open(const Rml::Element* _parent, const Rml::Vector2f& _position, uint32_t _itemsPerColumn = 16);
		void close();

		bool isOpen() const;

		void ProcessEvent(Rml::Event& _event) override;

		void runModal(const Rml::Element* _parent, const Rml::Vector2f& _position, uint32_t _itemsPerColumn = 100);
		void runModal(const Rml::Event& _mouseEvent, uint32_t _itemsPerColumn = 100);

		uint32_t getItemsPerColumn() const { return m_itemsPerColumn; }
		uint32_t getItemsPerColumn(uint32_t _default) const;
		void setItemsPerColumn(uint32_t _itemsPerColumn) { m_itemsPerColumn = _itemsPerColumn; }

	private:
		void closeAll();

		void setParentMenu(const std::shared_ptr<Menu>& _menu);
		void openSubmenu(const Rml::ObserverPtr<Rml::Element>& _parentEntry, const std::shared_ptr<Menu>& _submenu);
		void closeSubmenu();

		bool isChildOfThis(const Rml::Element* _elem, bool _checkSubmenu = true, bool _checkParentmenu = true) const;

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
		std::shared_ptr<Menu> m_parentMenu;

		std::unique_ptr<DelayedCall> m_openSubmenuDelay;

		uint32_t m_itemsPerColumn = UnknownItemsPerColumn;
	};
}
