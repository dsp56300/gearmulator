#include "condition.h"

#include "editor.h"

namespace genericUI
{
	Condition::Condition(juce::Component& _target, juce::Value& _value, std::set<uint8_t> _values) : m_target(_target), m_value(_value), m_values(std::move(_values))
	{
		m_value.addListener(this);
		valueChanged(m_value);
	}

	Condition::~Condition()
	{
		m_value.removeListener(this);
	}

	void Condition::valueChanged(juce::Value& _value)
	{
		const auto v = roundToInt(_value.getValueSource().getValue());

		const auto enable = m_values.find(static_cast<uint8_t>(v)) != m_values.end();

		Editor::setEnabled(m_target, enable);
	}
}
