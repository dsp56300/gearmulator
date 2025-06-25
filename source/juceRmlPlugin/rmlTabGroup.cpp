#include "rmlTabGroup.h"

#include "RmlUi/Core/Element.h"

namespace rmlPlugin
{
	TabGroup::~TabGroup()
	{
		for (size_t i=0; i<m_buttons.size(); ++i)
			setButton(nullptr, i);
	}

	void TabGroup::setPage(Rml::Element* _page, const size_t _index)
	{
		resize(_index + 1);

		m_pages[_index] = _page;

		setPageActive(_index, m_activePage == _index);
	}

	void TabGroup::setButton(Rml::Element* _button, const size_t _index)
	{
		resize(_index + 1);

		if (m_buttons[_index])
			m_buttons[_index]->RemoveEventListener(Rml::EventId::Click, this);

		m_buttons[_index] = _button;

		if (m_buttons[_index])
			m_buttons[_index]->AddEventListener(Rml::EventId::Click, this);

		setPageActive(_index, m_activePage == _index);
	}

	void TabGroup::ProcessEvent(Rml::Event& _event)
	{
		for (size_t i=0; i<m_buttons.size(); ++i)
		{
			if (_event.GetCurrentElement() == m_buttons[i])
				onClick(i);
		}
	}

	void TabGroup::setActivePage(const size_t _index)
	{
		if (m_activePage == _index)
			return;
		setPageActive(m_activePage, false);
		m_activePage = _index;
		setPageActive(m_activePage, true);
	}

	void TabGroup::resize(const size_t _size)
	{
		if (m_buttons.size() >= _size)
			return;
		m_buttons.resize(_size, nullptr);
		m_pages.resize(_size, nullptr);
	}

	void TabGroup::onClick(const size_t _index)
	{
		setActivePage(_index);
	}

	void TabGroup::setPageActive(const size_t _index, const bool _active) const
	{
		if (_index < m_pages.size())
		{
			if (auto* page = m_pages[_index])
			{
				if (_active)
					page->RemoveProperty(Rml::PropertyId::Display);
				else
					page->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
			}
		}

		if (_index < m_buttons.size())
		{
			if (auto* button = m_buttons[_index])
				button->SetPseudoClass("checked", _active);
		}
	}
}
