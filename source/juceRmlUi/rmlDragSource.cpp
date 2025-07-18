#include "rmlDragSource.h"

#include "RmlUi/Core/Element.h"

#include "rmlEventListener.h"
#include "rmlDragData.h"

namespace juceRmlUi
{
	DragSource::DragSource(Rml::Element* _element)
	{
		if (_element)
			init(_element);
	}

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

	DragSource* DragSource::fromElement(const Rml::Element* _elem)
	{
		if (!_elem)
			return nullptr;
		void* p = _elem->GetAttribute("dragSource", static_cast<void*>(nullptr));
		return static_cast<DragSource*>(p);
	}

	void DragSource::onDragStart(const Rml::Event& _event)
	{
		m_dragData = createDragData();

		m_element->SetAttribute("dragData", m_dragData.get());
		m_element->SetAttribute("dragSource", this);
	}

	void DragSource::onDragEnd(const Rml::Event& _event)
	{
		m_dragData.reset();
		m_element->RemoveAttribute("dragData");
		m_element->RemoveAttribute("dragSource");
	}
}
