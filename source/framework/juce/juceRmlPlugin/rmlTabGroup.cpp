#include "rmlTabGroup.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlHelper.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/DataModelHandle.h"
#include "RmlUi/Core/Element.h"

namespace rmlPlugin
{
	namespace
	{
		// Set on the page that is showing, and on the one playing its hide animation. A skin hangs its
		// animations on these; they are inert otherwise.
		constexpr const char* g_classActive  = "tabpage-active";
		constexpr const char* g_classLeaving = "tabpage-leaving";

		// Opt-in attribute on a page element. Absent = hide immediately, as it always did.
		constexpr const char* g_attribHideAnim = "tabhideanim";
	}

	TabGroup::TabGroup(std::string _name, Rml::Context* _context) : m_name(std::move(_name))
	{
		auto model = _context->CreateDataModel("tabgroup_" + m_name);

		model.Bind("page", &m_activePage);

		m_dataModel = model.GetModelHandle();
	}

	TabGroup::~TabGroup()
	{
		m_buttonListeners.clear();

		if (m_leavingPage)
			m_leavingPage->RemoveEventListener(Rml::EventId::Animationend, this);

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
		// End of a hide animation: now the page may actually be taken out of the layout.
		if (_event.GetId() == Rml::EventId::Animationend)
		{
			if (auto* page = _event.GetCurrentElement())
				finishHide(page);
			return;
		}

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
					// Button is being unchecked — check for taboffpage attribute
					if (auto* attrib = button->GetAttribute("taboffpage"))
					{
						const auto offPage = std::stoi(attrib->Get<Rml::String>(button->GetCoreInstance()));
						setActivePage(static_cast<size_t>(offPage));
						return;
					}

					setChecked(button, true);
					return;
				}
			}
		}
		setActivePage(_index);
	}

	void TabGroup::setPageActive(const size_t _index, const bool _active)
	{
		if (_index < m_pages.size())
		{
			Rml::Dictionary parameters;
			parameters["tab_index"]  = static_cast<int>(_index);

			if (auto* page = m_pages[_index])
			{
				if (_active)
				{
					// Cancels a hide that is still playing, so reopening at once shows the page rather than
					// letting it fade away underneath.
					if (m_leavingPage == page)
					{
						page->RemoveEventListener(Rml::EventId::Animationend, this);
						page->SetClass(g_classLeaving, false);
						m_leavingPage = nullptr;
					}
					page->RemoveProperty(Rml::PropertyId::Display);
				}
				// Only the page that is being left animates out. Setting up the initial state (setPage /
				// setButton) hides the inactive pages too, and those must not play their hide animation.
				else if (_index == m_activePage && hasHideAnimation(page))
				{
					beginHide(page);
				}
				else
				{
					page->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
				}

				// Showing a page only changes 'display', and RmlUi restarts an animation solely when the
				// 'animation' property itself changes (Element::HandleAnimationProperty, guarded by
				// dirty_animation). A stylesheet animation on the page would therefore play once at load,
				// while the page is still hidden, and never again. Carry the state as a class as well so a
				// skin can attach the animation to it and have it run on every activation. Inert unless a
				// skin styles it.
				page->SetClass(g_classActive, _active);

				page->DispatchEvent(Rml::EventId::Tabchange, parameters);
			}
		}

		if (_index < m_buttons.size())
		{
			for (auto* button : m_buttons[_index])
				setChecked(button, _active);
		}
	}

	bool TabGroup::hasHideAnimation(const Rml::Element* _page)
	{
		return _page && _page->GetAttribute(g_attribHideAnim) != nullptr;
	}

	void TabGroup::beginHide(Rml::Element* _page)
	{
		// Only one page of a group leaves at a time. A hide that is still running has had its chance, and
		// finishing it here also bounds a page whose animation never reports an end.
		if (m_leavingPage && m_leavingPage != _page)
			finishHide(m_leavingPage);

		// Leave 'display' alone - an element that is not displayed cannot animate - and mark it instead. The
		// skin's animation on .tabpage-leaving decides how long this takes; we wait for its animationend
		// rather than duplicating the duration here.
		m_leavingPage = _page;
		_page->SetClass(g_classLeaving, true);
		_page->AddEventListener(Rml::EventId::Animationend, this);
	}

	void TabGroup::finishHide(Rml::Element* _page)
	{
		_page->RemoveEventListener(Rml::EventId::Animationend, this);
		_page->SetClass(g_classLeaving, false);
		_page->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);

		if (m_leavingPage == _page)
			m_leavingPage = nullptr;
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
