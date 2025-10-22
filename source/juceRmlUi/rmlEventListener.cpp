#include "rmlEventListener.h"

#include "juceRmlComponent.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"

namespace juceRmlUi
{
	EventListener::EventListener(Rml::Element* _element, Rml::EventId _event, const std::function<void(Rml::Event&)>& _callback, bool _inCapturePhase/* = false*/)
	: m_element(_element)
	, m_event(_event)
	, m_callback(_callback)
	, m_inCapturePhase(_inCapturePhase)
	{
		_element->AddEventListener(_event, this, _inCapturePhase);
	}

	EventListener::~EventListener()
	{
		if (m_element)
			m_element->RemoveEventListener(m_event, this, m_inCapturePhase);
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

	void OnDetachListener::add(Rml::Element* _element, const Callback& _callback)
	{
		new OnDetachListener(_element, _callback);
	}

	OnDetachListener::OnDetachListener(Rml::Element* _element, Callback _callback, bool _autoDestroy/* = true*/) : m_callback(std::move(_callback)), m_element(_element), m_autoDestroy(_autoDestroy)
	{
		_element->AddEventListener(Rml::EventId::Unload, this);
	}

	OnDetachListener::~OnDetachListener()
	{
		if (m_element)
			m_element->RemoveEventListener(Rml::EventId::Unload, this);
	}

	void OnDetachListener::OnDetach(Rml::Element* _element)
	{
		if (_element != m_element)
			return;
		m_element = nullptr;
		m_callback(_element);
		if (m_autoDestroy)
			delete this;
	}

	ScopedListener::ScopedListener(Rml::Element* _elem, const Rml::EventId _id, const Callback& _callback, const bool _inCapturePhase)
	{
		add(_elem, _id, _callback, _inCapturePhase);
	}

	ScopedListener::~ScopedListener()
	{
		reset();
	}

	void ScopedListener::add(Rml::Element* _elem, const Rml::EventId _id, const Callback& _callback, const bool _inCapturePhase)
	{
		reset();

		m_element = _elem;
		m_eventId = _id;
		m_callback = _callback;
		m_inCapturePhase = _inCapturePhase;

		_elem->AddEventListener(_id, this, _inCapturePhase);
	}

	void ScopedListener::reset()
	{
		if (!m_element)
			return;

		m_element->RemoveEventListener(m_eventId, this, m_inCapturePhase);
		m_element = nullptr;
		m_eventId = Rml::EventId::Invalid;
		m_callback = nullptr;
	}

	void ScopedListener::ProcessEvent(Rml::Event& _event)
	{
		m_callback(_event);
	}

	DelayedCall::DelayedCall(Rml::Element* _element, const float _delaySeconds, const std::function<void()>& _callback, const bool _autoDestroy/* = true*/)
		: OnDetachListener(_element, [](Rml::Element*) {}, _autoDestroy)
		, m_targetTime(_element->GetCoreInstance().system_interface->GetElapsedTime() + _delaySeconds)
		, m_callback(_callback)
	{
		auto* comp = RmlComponent::fromElement(_element);

		getElement()->GetContext()->RequestNextUpdate(_delaySeconds);

		m_onUpdate.set(comp->evPostUpdate, [this](RmlComponent*)
		{
			onUpdate();
		});
	}

	DelayedCall* DelayedCall::create(Rml::Element* _element, const float _delaySeconds, const std::function<void()>& _callback, bool _autoDestroy/* = true*/)
	{
		return new DelayedCall(_element, _delaySeconds, _callback, _autoDestroy);
	}

	void DelayedCall::onUpdate()
	{
		const auto t = getElement()->GetCoreInstance().system_interface->GetElapsedTime();

		if (t < m_targetTime)
		{
			getElement()->GetContext()->RequestNextUpdate(m_targetTime - t);
			return;
		}

		getElement()->GetContext()->RequestNextUpdate(0);

		// do not fire again. We cannot remove our event listener here immediately because it does not support altering the list while invoking
		m_targetTime = std::numeric_limits<double>::max();

		if (getAutoDestroy())
		{
			auto c = m_callback;
			delete this;
			c();
		}
		else
		{
			m_callback();
		}
	}
}
