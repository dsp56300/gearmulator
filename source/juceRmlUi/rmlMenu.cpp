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
		if (m_root)
		{
			m_root->GetParentNode()->RemoveChild(m_root);
			m_root = nullptr;
		}

		if (m_document)
		{
			m_document->RemoveEventListener(Rml::EventId::Click, this);
		}
	}

	void Menu::addEntry(const std::string& _name, std::function<void()> _action)
	{
		addEntry(_name, false, std::move(_action));
	}

	void Menu::addEntry(const std::string& _name, const bool _checked, std::function<void()> _action)
	{
		m_entries.push_back({ _name, _checked, std::move(_action) });
	}

	void Menu::open(const Rml::Element* _parent, const Rml::Vector2f& _position, const uint32_t _itemsPerColumn)
	{
		auto* context = _parent->GetContext();
		auto* doc = _parent->GetOwnerDocument();

		auto menu = doc->CreateElement("div");

		menu->SetClass("dropdownbox", true);

		uint32_t counter = 0;

		Rml::Element* column = nullptr;

		for (const auto& entry : m_entries)
		{
			if (counter == 0)
			{
				auto c = doc->CreateElement("div");
				c->SetClass("dropdowncolumn", true);
				column = menu->AppendChild(std::move(c), true);
			}
			++counter;
			if (counter == _itemsPerColumn)
				counter = 0;

			auto div = doc->CreateElement("div");

			div->SetClass("dropdownoption", true);

			if (entry.checked)
				div->SetPseudoClass("checked", true);

			div->SetInnerRML(Rml::StringUtilities::EncodeRml(entry.name));

			if (entry.action)
			{
				juceRmlUi::EventListener::Add(div, Rml::EventId::Click, [this, action = entry.action](Rml::Event& _event)
				{
					action();
					_event.StopPropagation();
					close();
				});
			}
			column->AppendChild(std::move(div), true);
		}

		// place at mouse position
        menu->SetProperty("position", "absolute");
        menu->SetProperty("left", std::to_string(_position.x) + "px");
        menu->SetProperty("top", std::to_string(_position.y) + "px");

		m_root = doc->AppendChild(std::move(menu), true);

		auto dims = Rml::Vector2f(context->GetDimensions());

		context->Update();

		// make sure the dropdown is not outside the document bounds
		const auto box = m_root->GetBox();
		auto size = box.GetSize();
		if (_position.x + size.x > dims.x)
			m_root->SetProperty("left", std::to_string(dims.x - size.x) + "px");
		if (_position.y + size.y > dims.y)
			m_root->SetProperty("top", std::to_string(dims.y - size.y) + "px");

		m_document = _parent->GetOwnerDocument();
		m_document->AddEventListener(Rml::EventId::Click, this);
	}

	void Menu::close()
	{
		delete this;
	}

	void Menu::ProcessEvent(Rml::Event& _event)
	{
		close();
	}
}
