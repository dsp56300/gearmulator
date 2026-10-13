#include "rmlElemSplitter.h"

#include <algorithm>

#include "rmlHelper.h"

#include "RmlUi/Core/ComputedValues.h"

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
			freezeSizes();
		}
		else if (_event.GetId() == Rml::EventId::Drag)
		{
			auto mousePos = helper::getMousePos(_event);
			processMouseMove(mousePos);
		}
	}

	bool ElemSplitter::isVertical() const
	{
		// between the children of a column it moves up and down
		const auto direction = GetParentNode()->GetComputedValues().flex_direction();
		return direction == Rml::Style::FlexDirection::Column || direction == Rml::Style::FlexDirection::ColumnReverse;
	}

	void ElemSplitter::freezeSizes()
	{
		// The basis of a growing child is not its size, and a basis in dp is not one in px: a drag starts from the
		// laid out sizes. That does not change the layout, there is no free space left to grow into
		const auto vertical = isVertical();

		for (int i=0; i<GetParentNode()->GetNumChildren(); ++i)
		{
			auto* child = GetParentNode()->GetChild(i);

			if (dynamic_cast<ElemSplitter*>(child))
				continue;

			const auto& computed = child->GetComputedValues();
			if (computed.display() == Rml::Style::Display::None)
				continue;

			const auto area = computed.box_sizing() == Rml::Style::BoxSizing::BorderBox ? Rml::BoxArea::Border : Rml::BoxArea::Content;
			const auto size = child->GetBox().GetSize(area);
			helper::changeProperty(child, Rml::PropertyId::FlexBasis, Rml::Property(vertical ? size.y : size.x, Rml::Unit::PX));
		}
	}

	void ElemSplitter::processMouseMove(const Rml::Vector2f _mousePos)
	{
		auto* prev = GetPreviousSibling();
		auto* next = GetNextSibling();

		const auto vertical = isVertical();

		auto delta = vertical ? _mousePos.y - m_lastMousePos.y : _mousePos.x - m_lastMousePos.x;

		auto getFlexBasis = [](Rml::Element* _elem)
		{
			auto* prop = _elem ? _elem->GetProperty(Rml::PropertyId::FlexBasis) : nullptr;
			return prop ? prop->Get<float>(_elem->GetCoreInstance()) : 0.0f;
		};

		// neither of the two gets smaller than nothing, the splitter waits for a mouse that went further
		if (prev)
			delta = std::max(delta, -getFlexBasis(prev));
		if (next)
			delta = std::min(delta, getFlexBasis(next));

		auto setFlexBasis = [&](Rml::Element* _elem, float _delta)
		{
			if (_elem)
				helper::changeProperty(_elem, Rml::PropertyId::FlexBasis, Rml::Property(getFlexBasis(_elem) + _delta, Rml::Unit::PX));
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

		if (vertical)
			m_lastMousePos.y += delta;
		else
			m_lastMousePos.x += delta;
	}
}
