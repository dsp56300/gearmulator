#include "rmlMenu.h"

#include "rmlEventListener.h"
#include "rmlHelper.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

namespace juceRmlUi
{
	Menu::~Menu()
	{
		close();
	}

	void Menu::addEntry(const std::string& _name, std::function<void()> _action)
	{
		addEntry(_name, false, std::move(_action));
	}

	void Menu::addEntry(const std::string& _name, const bool _checked, std::function<void()> _action)
	{
		m_entries.push_back({ _name, _checked, false, true, std::move(_action), {} });
	}

	void Menu::addSeparator()
	{
		m_entries.push_back({ {}, false, true, true, {}, {} });
	}

	void Menu::addSubMenu(const std::string& _name, const std::shared_ptr<Menu>& _subMenu)
	{
		if (_subMenu)
			m_entries.push_back({ _name, false, false, true, {}, _subMenu });
	}

	void Menu::open(const Rml::Element* _parent, const Rml::Vector2f& _position, const uint32_t _itemsPerColumn)
	{
		if (!isOpen())
			close();

		auto* context = _parent->GetContext();
		auto* doc = _parent->GetOwnerDocument();

		auto menu = doc->CreateElement("div");

		menu->SetClass("menubox", true);

		uint32_t counter = 0;

		Rml::Element* column = nullptr;

		for (const auto& entry : m_entries)
		{
			// ignore separators at start/end of a column
			if (entry.separator)
			{
				if (counter == 0 || counter == (_itemsPerColumn - 1))
					continue;
			}

			if (counter == 0)
			{
				auto c = doc->CreateElement("div");
				c->SetClass("menucolumn", true);
				column = menu->AppendChild(std::move(c), true);
			}
			++counter;
			if (counter == _itemsPerColumn)
				counter = 0;

			auto div = doc->CreateElement("div");

			div->SetClass("menuitem", true);

			if (entry.submenu)
				div->SetPseudoClass("submenu", true);
			if (entry.checked)
				div->SetPseudoClass("checked", true);
			else if (entry.separator)
				div->SetPseudoClass("separator", true);
			if (!entry.enabled)
				div->SetPseudoClass("disabled", true);

			if (entry.submenu)
			{
				Rml::ObserverPtr<Rml::Element> parent = div->GetObserverPtr();
				juceRmlUi::EventListener::Add(div, Rml::EventId::Mouseover, [this, parent, submenu = entry.submenu](Rml::Event& _event)
				{
					openSubmenu(parent, submenu);
					_event.StopPropagation();
				});
			}
			else
			{
				juceRmlUi::EventListener::Add(div, Rml::EventId::Mouseover, [this](Rml::Event& _event)
				{
					closeSubmenu();
				});
			}
			if (!entry.separator && entry.action && entry.enabled)
			{
				juceRmlUi::EventListener::Add(div, Rml::EventId::Click, [this, action = entry.action](Rml::Event& _event)
				{
					action();
					_event.StopPropagation();
					closeAll();
				});
			}

			div->SetInnerRML(Rml::StringUtilities::EncodeRml(entry.name));

			column->AppendChild(std::move(div), true);
		}

		// place at provided position
        menu->SetProperty("position", "absolute");
        menu->SetProperty("left", std::to_string(_position.x) + "px");
        menu->SetProperty("top", std::to_string(_position.y) + "px");

		m_root = doc->AppendChild(std::move(menu), true);

		auto dims = Rml::Vector2f(context->GetDimensions());

		context->Update();

		// make sure the dropdown is not outside the document bounds
		const auto box = m_root->GetBox();
		auto size = box.GetSize(Rml::BoxArea::Border);
		if (_position.x + size.x > dims.x)
			m_root->SetProperty("left", std::to_string(dims.x - size.x) + "px");
		if (_position.y + size.y > dims.y)
			m_root->SetProperty("top", std::to_string(dims.y - size.y) + "px");

		m_document = _parent->GetOwnerDocument();
		m_document->AddEventListener(Rml::EventId::Click, this);

		m_root->AddEventListener(Rml::EventId::Mouseover, this);
	}

	void Menu::close()
	{
		closeSubmenu();

		Rml::Element* root = nullptr;

		if (m_root)
		{
			root = m_root;
			m_root->RemoveEventListener(Rml::EventId::Mouseover, this);
			m_root = nullptr;
		}

		if (m_document)
		{
			m_document->RemoveEventListener(Rml::EventId::Click, this);
			m_document = nullptr;
		}

		m_parentMenu = nullptr;

		if (root)
			root->GetParentNode()->RemoveChild(root);
	}

	bool Menu::isOpen() const
	{
		return m_root != nullptr;
	}

	void Menu::ProcessEvent(Rml::Event& _event)
	{
		switch (_event.GetId())
		{
		case Rml::EventId::Click:
			{
				const auto* target = _event.GetTargetElement();
				if (target && helper::isChildOf(m_root, target))
					return;
				Rml::Log::Message(Rml::Log::LT_INFO, "somewhere else clicked, closing");
				close();
			}
			break;
		default:
			return;
		}
	}

	void Menu::runModal(const Rml::Element* _parent, const Rml::Vector2f& _position, const uint32_t _itemsPerColumn)
	{
		if (isOpen())
			return;

		// we want to keep the menu alive until it is closed. move it into a unique_ptr
		auto m = std::make_shared<Menu>(std::move(*this));

		m->open(_parent, _position, _itemsPerColumn);

		const auto root = m->m_root;

		OnDetachListener::add(root, [menu = std::move(m)](Rml::Element*) mutable
		{
			menu.reset();
		});
	}

	void Menu::runModal(const Rml::Event& _mouseEvent, uint32_t _itemsPerColumn)
	{
		runModal(_mouseEvent.GetTargetElement(), helper::getMousePos(_mouseEvent), _itemsPerColumn);
	}

	void Menu::closeAll()
	{
		if (!m_parentMenu)
		{
			close();
			return;
		}

		const auto p = m_parentMenu;
		m_parentMenu.reset();
		p->closeAll();
	}

	void Menu::setParentMenu(const std::shared_ptr<Menu>& _menu)
	{
		m_parentMenu = _menu;
	}

	void Menu::openSubmenu(const Rml::ObserverPtr<Rml::Element>& _parentEntry, const std::shared_ptr<Menu>& _submenu)
	{
		if (m_subMenu == _submenu)
			return;
		closeSubmenu();
		m_subMenu = _submenu;
		m_subMenuParentEntry = _parentEntry;

		if (!m_subMenu)
			return;

		m_subMenuParentEntry->SetPseudoClass("active", true);

		auto pos = _parentEntry->GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto size = _parentEntry->GetBox().GetSize(Rml::BoxArea::Border);
		pos += Rml::Vector2f(size.x, 0);
		m_subMenu->open(_parentEntry.get(), pos, 16);
		m_subMenu->setParentMenu(shared_from_this());
	}

	void Menu::closeSubmenu()
	{
		if (m_subMenu)
			m_subMenu->close();
		m_subMenu.reset();
		if (m_subMenuParentEntry)
			m_subMenuParentEntry->SetPseudoClass("active", false);
		m_subMenuParentEntry.reset();
	}
}
