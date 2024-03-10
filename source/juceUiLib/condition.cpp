#include "condition.h"

#include "editor.h"

namespace genericUI
{
	ConditionByParameterValues::ConditionByParameterValues(juce::Component& _target, juce::Value* _value, int32_t _parameterIndex, std::set<uint8_t> _values) : m_target(_target), m_parameterIndex(_parameterIndex), m_values(std::move(_values))
	{
		bind(_value);
	}

	ConditionByParameterValues::~ConditionByParameterValues()
	{
		unbind();
	}

	void ConditionByParameterValues::valueChanged(juce::Value& _value)
	{
		const auto v = roundToInt(_value.getValueSource().getValue());

		const auto enable = m_values.find(static_cast<uint8_t>(v)) != m_values.end();

		Editor::setEnabled(m_target, enable);
	}

	void ConditionByParameterValues::bind(juce::Value* _value)
	{
		unbind();

		m_value = _value;

		if(!m_value)
			return;

		m_value->addListener(this);
		valueChanged(*m_value);
	}

	void ConditionByParameterValues::unbind()
	{
		if(!m_value)
			return;

		m_value->removeListener(this);
		m_value = nullptr;
	}

	void ConditionByParameterValues::refresh()
	{
		if(m_value)
			valueChanged(*m_value);
	}

	void ConditionByParameterValues::setCurrentPart(const Editor& _editor, const uint8_t _part)
	{
		unbind();
		
		const auto v = _editor.getInterface().getParameterValue(getParameterIndex(), _part);
		if(v)
			bind(v);
	}
}
