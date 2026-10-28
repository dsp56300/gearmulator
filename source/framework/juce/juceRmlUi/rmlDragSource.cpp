#include "rmlDragSource.h"

#include <set>

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"

#include "juceRmlComponent.h"
#include "rmlEventListener.h"
#include "rmlDragData.h"
#include "rmlHelper.h"

namespace juceRmlUi
{
	namespace
	{
		// the sources alive, for the deferred end of a cancelled drag (message thread only)
		std::set<DragSource*>& liveSources()
		{
			static std::set<DragSource*> s;
			return s;
		}
	}

	DragSource::DragSource(Rml::Element* _element)
	{
		liveSources().insert(this);
		if (_element)
			init(_element);
	}

	DragSource::~DragSource()
	{
		liveSources().erase(this);
	}

	bool DragSource::init(Rml::Element* _element)
	{
		if (m_element)
			return false;

		m_element = _element;

		EventListener::Add(_element,Rml::EventId::Dragstart, [this](const Rml::Event& _event) { onDragStart(_event); });
		EventListener::Add(_element,Rml::EventId::Dragend, [this](const Rml::Event& _event) { onDragEnd(_event); });

		return true;
	}

	DragSource* DragSource::fromElement(const Rml::Element* _elem)
	{
		if (!_elem)
			return nullptr;
		return static_cast<DragSource*>(_elem->GetAttribute<void*>("dragSource", nullptr));
	}

	void DragSource::onDragStart(const Rml::Event& _event)
	{
		m_handled = false;
		m_cancelled = false;
		m_dragData = createDragData();

		if (!m_dragData)
			return;

		m_element->SetAttribute("dragData", m_dragData.get());
		m_element->SetAttribute("dragSource", this);

		if (auto* context = m_element->GetContext())
			m_escapeListener.add(context->GetRootElement(), Rml::EventId::Keydown, [this](Rml::Event& _e) { onKeyDown(_e); });
	}

	void DragSource::onDragEnd(const Rml::Event& _event)
	{
		m_dragData.reset();
		m_element->RemoveAttribute("dragData");
		m_element->RemoveAttribute("dragSource");

		// not from inside its own callback: a cancelled drag ends from a post-frame callback
		m_escapeListener.reset();
	}

	void DragSource::onKeyDown(Rml::Event& _event)
	{
		if (m_cancelled || !m_dragData || helper::getKeyIdentifier(_event) != Rml::Input::KI_ESCAPE)
			return;

		_event.StopPropagation();
		m_cancelled = true;

		// RmlUi has no call to end a drag. A release ends it, removes the drag clone and sends dragend,
		// and the targets refuse the drop of a cancelled drag. It is processed after this key event's
		// dispatch, which must not end this listener from inside its own callback.
		auto* comp = RmlComponent::fromElement(m_element);
		if (!comp)
			return;
		comp->addPostFrameCallback([this]
		{
			if (liveSources().count(this))
				endCancelledDrag();
		});
		comp->enqueueUpdate();
	}

	void DragSource::endCancelledDrag()
	{
		auto* context = m_element ? m_element->GetContext() : nullptr;
		if (!context || !m_dragData)
			return;

		// The release would also click the element the drag started on, which keeps the hover during a
		// drag: block clicks while it is processed.
		ScopedListener clickBlocker(context->GetRootElement(), Rml::EventId::Click, [](Rml::Event& _e)
		{
			_e.StopPropagation();
		});
		context->ProcessMouseButtonUp(0, 0);
	}
}
