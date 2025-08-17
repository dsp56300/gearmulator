#include "rmlControllerLink.h"

#include <stdexcept>

#include "rmlTabGroup.h"

#include "juceRmlUi/rmlElemValue.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace rmlPlugin
{
	ControllerLink::ControllerLink(Rml::Element* _source, Rml::Element* _target, Rml::Element* _conditionButton)
	: m_source(_source)
	, m_target(_target)
	, m_conditionButton(_conditionButton)
	{
		if (!m_source || !m_target || !m_conditionButton)
			throw std::runtime_error("ControllerLink: Source, target and condition button must not be null");

		m_source->AddEventListener(Rml::EventId::Mousedown, this);
		m_source->AddEventListener(Rml::EventId::Mouseup, this);
		m_source->AddEventListener(Rml::EventId::Change, this);

		m_lastSourceValue = getValue(m_source);
	}

	ControllerLink::~ControllerLink()
	{
		m_source->RemoveEventListener(Rml::EventId::Mousedown, this);
		m_source->RemoveEventListener(Rml::EventId::Mouseup, this);
		m_source->RemoveEventListener(Rml::EventId::Change, this);
	}

	void ControllerLink::ProcessEvent(Rml::Event& _event)
	{
		switch (_event.GetId())
		{
		case Rml::EventId::Mousedown:
			m_lastSourceValue = getValue(m_source);
			m_sourceIsBeingDragged = true;
			break;
		case Rml::EventId::Mouseup:
			m_sourceIsBeingDragged = false;
			break;
		case Rml::EventId::Change:
			{
				if (m_processingChangeEvent)
					break;
				const auto current = getValue(m_source);
				const auto delta = current - m_lastSourceValue;
				m_lastSourceValue = current;

				if(!m_sourceIsBeingDragged)
					return;

				if(std::abs(delta) < 0.0001f)
					return;

				if(m_conditionButton && !TabGroup::isChecked(m_conditionButton))
					return;

				const auto destValue = getValue(m_target);
				const auto newDestValue = destValue + delta;

				if(std::abs(newDestValue - destValue) < 0.0001)
					return;

				m_processingChangeEvent = true;
				setValue(m_target, newDestValue);
				m_processingChangeEvent = false;
				break;
			}
		default:;
		}
	}

	float ControllerLink::getValue(Rml::Element* _element)
	{
		if (auto* elemValue = dynamic_cast<juceRmlUi::ElemValue*>(_element))
			return elemValue->getValue();
		if (auto* input = dynamic_cast<Rml::ElementFormControl*>(_element))
			return input->GetValue().empty() ? 0.0f : std::stof(input->GetValue());
		return _element->GetAttribute("value", 0.0f);
	}

	void ControllerLink::setValue(Rml::Element* _element, const float _value)
	{
		if (auto* elemValue = dynamic_cast<juceRmlUi::ElemValue*>(_element))
			return elemValue->setValue(_value);
		if (auto* input = dynamic_cast<Rml::ElementFormControl*>(_element))
			return input->SetValue(std::to_string(_value));
		return _element->SetAttribute("value", _value);
	}
}
