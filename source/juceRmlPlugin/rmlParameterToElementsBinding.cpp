#include "rmlParameterToElementsBinding.h"

#include "rmlParameterBinding.h"

#include "juceRmlUi/rmlElemKnob.h"

#include "juce_events/juce_events.h"

namespace rmlPlugin
{
	ParameterToElementsBinding::ParameterToElementsBinding(RmlParameterBinding& _binding, pluginLib::Parameter* _parameter, Rml::Element* _firstElement) : m_binding(_binding), m_parameter(_parameter)
	{
		m_listener.set(_parameter, [this](pluginLib::Parameter* /*_p*/)
		{
			onParameterValueChanged();
		});

		if (m_parameter->getDescription().isSoftKnob())
		{
			auto* softknob = _binding.getController().getSoftknob(m_parameter);
			if (softknob)
			{
				m_onSoftKnobTargetChanged.set(softknob->onBind, [this](const pluginLib::Parameter* _param)
				{
					if (!_param)
						return;

					setElementsParameterDefaults(_param);
				});
			}
		}

		addElement(_firstElement);
	}

	ParameterToElementsBinding::~ParameterToElementsBinding()
	{
		while (!m_elements.empty())
		{
			auto it = m_elements.begin();
			(*it)->RemoveEventListener(Rml::EventId::Change, this);
		}
	}

	bool ParameterToElementsBinding::addElement(Rml::Element* _element)
	{
		if (!m_elements.insert(_element).second)
			return false;

		auto targetParam = m_parameter;

		if (m_parameter->getDescription().isSoftKnob())
		{
			const auto softknob = m_binding.getController().getSoftknob(m_parameter);

			if (softknob)
			{
				auto* target = softknob->getTargetParameter();
				if (target)
					targetParam = target;
			}
		}

		setElementParameterDefaults(_element, targetParam);

		setElementValue(_element, m_parameter);

		_element->AddEventListener(Rml::EventId::Change, this);

		return true;
	}

	bool ParameterToElementsBinding::removeElement(Rml::Element* _element)
	{
		if (m_elements.erase(_element) <= 0)
			return false;

		_element->RemoveEventListener(Rml::EventId::Change, this);

		return true;
	}

	void ParameterToElementsBinding::onParameterValueChanged()
	{
		if (juce::MessageManager::getInstance()->isThisTheMessageThread())
		{
			updateElementsFromParameter();
		}
		else
		{
			juce::MessageManager::callAsync([this]()
			{
				updateElementsFromParameter();
			});
		}
	}

	void ParameterToElementsBinding::updateElementsFromParameter()
	{
		const auto value = m_parameter->getUnnormalizedValue();

		IgnoreChangeEvents ignore(*this);

		for (auto* element : m_elements)
		{
			if (!element)
				continue;
			setElementValue(element, m_parameter);
		}
	}

	void ParameterToElementsBinding::setElementParameterDefaults(Rml::Element* _element, const pluginLib::Parameter* _parameter)
	{
		const auto& desc = _parameter->getDescription();
		const auto& range = desc.range;

		_element->SetAttribute("min", range.getStart());
		_element->SetAttribute("max", range.getEnd());
		_element->SetAttribute("default", _parameter->getDefault());
		_element->SetAttribute("step", _parameter->getDescription().step);
	}

	void ParameterToElementsBinding::setElementsParameterDefaults(const pluginLib::Parameter* _parameter) const
	{
		for (auto* element : m_elements)
			setElementParameterDefaults(element, _parameter);
	}

	void ParameterToElementsBinding::ProcessEvent(Rml::Event& _event)
	{
		if (_event == Rml::EventId::Change)
		{
			auto* element = _event.GetTargetElement();

			if (!element)
				return;

			const auto v = getElementValue(element, m_parameter);

			if (m_parameter->getUnnormalizedValue() == v)
				return;

			m_binding.registerPendingGesture(m_parameter);

			m_parameter->setUnnormalizedValueNotifyingHost(v, pluginLib::Parameter::Origin::Ui);

			// update other elements
			if (m_elements.size() > 1)
				updateElementsFromParameter();
		}
	}

	void ParameterToElementsBinding::OnDetach(Rml::Element* _element)
	{
		EventListener::OnDetach(_element);

		m_elements.erase(_element);
	}

	int ParameterToElementsBinding::getElementValue(const Rml::Element* _element, const pluginLib::Parameter* _default)
	{
		const auto v = _element->GetAttribute("value", _default->getUnnormalizedValue());

		if (isReversed(_element))
		{
			const auto& desc = _default->getDescription();
			const auto& range = desc.range;
			return range.getEnd() - (v - range.getStart());
		}
		return v;
	}

	void ParameterToElementsBinding::setElementValue(Rml::Element* _element, const pluginLib::Parameter* _source)
	{
		const auto currentValue = getElementValue(_element, _source);

		const auto newValue = _source->getUnnormalizedValue();

		if (newValue == currentValue)
			return;

		if (isReversed(_element))
		{
			const auto& desc = _source->getDescription();
			const auto& range = desc.range;
			const auto v = range.getEnd() - (newValue - range.getStart());
			_element->SetAttribute("value", v);
			return;
		}
		_element->SetAttribute("value", newValue);
	}

	bool ParameterToElementsBinding::isReversed(const Rml::Element* _element)
	{
		auto* attrib = _element->GetAttribute("orientation");
		if (!attrib)
			return false;
		return attrib->Get(_element->GetCoreInstance(), std::string()) == "vertical";
	}

	ParameterToElementsBinding::IgnoreChangeEvents::IgnoreChangeEvents(ParameterToElementsBinding& _owner) : m_owner(_owner)
	{
		++m_owner.m_ignoreChangeEvents;
	}

	ParameterToElementsBinding::IgnoreChangeEvents::~IgnoreChangeEvents()
	{
		assert(m_owner.m_ignoreChangeEvents > 0);
		--m_owner.m_ignoreChangeEvents;
	}
}
