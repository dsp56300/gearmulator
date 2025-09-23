#include "rmlElemSplitter.h"

#include "rmlHelper.h"

namespace juceRmlUi
{
	ElemSplitter::ElemSplitter(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : Element(_coreInstance, _tag)
	{
		AddEventListener(Rml::EventId::Mousedown, this);
		AddEventListener(Rml::EventId::Drag, this);

		SetProperty(Rml::PropertyId::Drag, Rml::Style::Drag::Drag);
	}

	void ElemSplitter::ProcessEvent(Rml::Event& _event)
	{
		if (_event.GetId() == Rml::EventId::Mousedown)
		{
			m_lastMousePos = helper::getMousePos(_event);
		}
		else if (_event.GetId() == Rml::EventId::Drag)
		{
			auto mousePos = helper::getMousePos(_event);
			processMouseMove(mousePos);
		}
	}

	void ElemSplitter::processMouseMove(const Rml::Vector2f _mousePos)
	{
		auto* prev = GetPreviousSibling();
		auto* next = GetNextSibling();

		const auto delta = _mousePos.x - m_lastMousePos.x;

		auto setFlexBasis = [](Rml::Element* _elem, float _delta)
		{
			if (_elem)
			{
				float w = 0.0f;
				auto* prop = _elem->GetProperty(Rml::PropertyId::FlexBasis);
				if (prop)
					w = prop->Get<float>(_elem->GetCoreInstance());
				w += _delta;
				helper::changeProperty(_elem, Rml::PropertyId::FlexBasis, Rml::Property(w, Rml::Unit::PX));
			}
		};

		for (int i=0; i<GetParentNode()->GetNumChildren(); ++i)
		{
			auto* child = GetParentNode()->GetChild(i);

			if (dynamic_cast<ElemSplitter*>(child))
				continue;
			if (child == prev)
				setFlexBasis(child, delta);
			else if (child == next)
				setFlexBasis(child, -delta);
			else
				setFlexBasis(child, 0);
		}

		m_lastMousePos = _mousePos;
	}
}
