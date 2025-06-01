#include "rmlEventListener.h"

#include "RmlUi/Core/Element.h"

namespace juceRmlUi
{
	EventListener::EventListener(Rml::Element* _element, Rml::EventId _event, const std::function<void(Rml::Event&)>& _callback)
	: m_element(_element)
	, m_event(_event)
	, m_callback(_callback)
	{
		_element->AddEventListener(_event, this);
	}

	EventListener::~EventListener()
	{
		if (m_element)
			m_element->RemoveEventListener(m_event, this);
	}

	void EventListener::ProcessEvent(Rml::Event& _event)
	{
		m_callback(_event);
	}

	void EventListener::OnDetach(Rml::Element* _element)
	{
		if (m_element == _element)
		{
			m_element = nullptr;
			delete this;
		}
	}
}
