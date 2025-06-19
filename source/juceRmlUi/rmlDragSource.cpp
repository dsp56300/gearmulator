#include "rmlDragSource.h"

#include "RmlUi/Core/Element.h"

#include "rmlDragData.h"
#include "rmlEventListener.h"

namespace juceRmlUi
{
	DragSource::DragSource()
	= default;

	DragSource::~DragSource()
	= default;

	bool DragSource::init(Rml::Element* _element)
	{
		if (m_element)
			return false;

		m_element = _element;

		EventListener::Add(_element,Rml::EventId::Dragstart, [this](const Rml::Event& _event) { onDragStart(_event); });
		EventListener::Add(_element,Rml::EventId::Dragend, [this](const Rml::Event& _event) { onDragEnd(_event); });

		return true;
	}

	void DragSource::onDragStart(const Rml::Event& _event)
	{
		m_dragData = createDragData();
	}

	void DragSource::onDragEnd(const Rml::Event& _event)
	{
		m_dragData.reset();
	}
}
