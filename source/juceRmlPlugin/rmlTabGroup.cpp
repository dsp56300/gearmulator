#include "rmlTabGroup.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlHelper.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/DataModelHandle.h"
#include "RmlUi/Core/Element.h"

namespace rmlPlugin
{
	TabGroup::TabGroup(std::string _name, Rml::Context* _context) : m_name(std::move(_name))
	{
		auto model = _context->CreateDataModel("tabgroup_" + m_name);

		model.Bind("page", &m_activePage);

		m_dataModel = model.GetModelHandle();
	}

	TabGroup::~TabGroup()
	{
		m_buttonListeners.clear();

		for (auto& buttons : m_buttons)
		{
			for (auto* button : buttons)
				button->RemoveEventListener(Rml::EventId::Click, this);
		}
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

		if (std::find(m_buttons[_index].begin(), m_buttons[_index].end(), _button) != m_buttons[_index].end())
			return;

		m_buttons[_index].push_back(_button);

		m_buttonListeners[_index].emplace_back();

		if (auto* button = dynamic_cast<juceRmlUi::ElemButton*>(m_buttons[_index].back()))
			m_buttonListeners[_index].back().set(button->evClick, [this, _index](juceRmlUi::ElemButton*) { onClick(_index); });
		else
			m_buttons[_index].back()->AddEventListener(Rml::EventId::Click, this);

		setPageActive(_index, m_activePage == _index);
	}

	void TabGroup::ProcessEvent(Rml::Event& _event)
	{
		for (size_t i=0; i<m_buttons.size(); ++i)
		{
			for(const auto* button : m_buttons[i])
			{
				if (_event.GetCurrentElement() == button)
					onClick(i);
			}
		}
	}

	void TabGroup::setActivePage(const size_t _index)
	{
		if (m_activePage == _index)
			return;

		setPageActive(m_activePage, false);

		m_activePage = static_cast<uint32_t>(_index);
		m_dataModel.DirtyVariable("page");

		setPageActive(m_activePage, true);
	}

	void TabGroup::resize(const size_t _size)
	{
		if (m_buttons.size() >= _size)
			return;

		m_buttons.resize(_size);
		m_buttonListeners.resize(_size);

		m_pages.resize(_size, nullptr);
	}

	void TabGroup::onClick(const size_t _index)
	{
		if (_index < m_buttons.size())
		{
			for (auto* button : m_buttons[_index])
			{
				if (button && isToggle(button) && !isChecked(button))
				{
					setChecked(button, true);
					return;
				}
			}
		}
		setActivePage(_index);
	}

	void TabGroup::setPageActive(const size_t _index, const bool _active) const
	{
		if (_index < m_pages.size())
		{
			Rml::Dictionary parameters;
			parameters["tab_index"] = _index;

			if (auto* page = m_pages[_index])
			{
				if (_active)
					page->RemoveProperty(Rml::PropertyId::Display);
				else
					page->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);

				page->DispatchEvent(Rml::EventId::Tabchange, parameters);
			}
		}

		if (_index < m_buttons.size())
		{
			for (auto* button : m_buttons[_index])
				setChecked(button, _active);
		}
	}

	bool TabGroup::isChecked(const Rml::Element* _button)
	{
		return juceRmlUi::ElemButton::isChecked(_button);
	}

	void TabGroup::setChecked(Rml::Element* _button, const bool _checked)
	{
		juceRmlUi::ElemButton::setChecked(_button, _checked);
	}

	bool TabGroup::isToggle(const Rml::Element* _button)
	{
		return juceRmlUi::ElemButton::isToggle(_button);
	}

	bool TabGroup::selectTabWithElement(const Rml::Element* _element)
	{
		for (size_t i = 0; i < m_pages.size(); ++i)
		{
			if (m_pages[i] && juceRmlUi::helper::isChildOf(m_pages[i], _element))
			{
				setActivePage(i);
				return true;
			}
		}

		return false;
	}
}
