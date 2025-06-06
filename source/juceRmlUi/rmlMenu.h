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
		~Menu() override;

		void addEntry(const std::string& _name, std::function<void()> _action);
		void addEntry(const std::string& _name, const bool _checked, std::function<void()> _action);
		void addSeparator();

		void clear()
		{
			m_entries.clear();
		}

		void open(const Rml::Element* _parent, const Rml::Vector2f& _position, uint32_t _itemsPerColumn = 16);
		void close();

		void ProcessEvent(Rml::Event& _event) override;

	private:
		struct Entry
		{
			std::string name;
			bool checked = false;
			bool separator = false;
			std::function<void()> action;
		};

		std::vector<Entry> m_entries;
		Rml::Element* m_root = nullptr;
		Rml::ElementDocument* m_document = nullptr;
	};
}
