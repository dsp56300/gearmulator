#include "rmlElemValue.h"

namespace juceRmlUi
{
	namespace
	{
		constexpr auto g_attribValue = "value";
		constexpr auto g_attribMinValue = "min";
		constexpr auto g_attribMaxValue = "max";
	}

	void ElemValue::OnAttributeChange(const Rml::ElementAttributes& _changedAttributes)
	{
		Element::OnAttributeChange(_changedAttributes);

		auto checkAttribute = [this, &_changedAttributes](const char* _key, float& _property, const baseLib::Event<float>& _ev)
		{
			const auto it = _changedAttributes.find(_key);

			if (it != _changedAttributes.end())
			{
				auto v = GetAttribute<float>("value", 0.0f);
				setProperty(_property, v, _ev);
			}
		};

		checkAttribute(g_attribValue, m_value, onValueChanged);
		checkAttribute(g_attribMinValue, m_min, onMinValueChanged);
		checkAttribute(g_attribMaxValue, m_max, onMaxValueChanged);
	}

	void ElemValue::setValue(const float _value)
	{
		const auto v = std::clamp(_value, m_min, m_max);

		if (setProperty(m_value, v, onValueChanged))
			SetAttribute(g_attribValue, v);
	}

	void ElemValue::setMinValue(const float _value)
	{
		if (!setProperty(m_min, _value, onMinValueChanged))
			return;
		SetAttribute(g_attribMinValue, _value);
		setValue(getValue());	// update value if it is out of range
	}

	void ElemValue::setMaxValue(const float _value)
	{
		if (!setProperty(m_max, _value, onMaxValueChanged))
			return;
		SetAttribute(g_attribMaxValue, _value);
		setValue(getValue());	// update value if it is out of range
	}

	bool ElemValue::setProperty(float& _prop, const float _newValue, const baseLib::Event<float>& _event)
	{
		if (_prop == _newValue)  // NOLINT(clang-diagnostic-float-equal)
			return false;
		_prop = _newValue;
		_event(_newValue);
		return true;
	}
}
