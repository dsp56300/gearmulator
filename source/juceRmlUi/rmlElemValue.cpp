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

		auto checkAttribute = [this, &_changedAttributes](const char* _key, const std::function<void(float)>& _setter)
		{
			const auto it = _changedAttributes.find(_key);

			if (it != _changedAttributes.end())
			{
				auto v = GetAttribute<float>(_key, 0.0f);
				_setter(v);
			}
		};

		checkAttribute(g_attribValue, [&](const float _x) { setValue(_x); });
		checkAttribute(g_attribMinValue, [&](const float _x) { setMinValue(_x); });
		checkAttribute(g_attribMaxValue, [&](const float _x) { setMaxValue(_x); });
	}

	void ElemValue::setValue(const float _value, bool _sendChangeEvent/* = true*/)
	{
		const auto v = m_max > m_min ? std::clamp(_value, m_min, m_max) : m_min;

		if (!setProperty(m_value, v, onValueChanged))
			return;

		SetAttribute(g_attribValue, v);
		onChangeValue();
		if (_sendChangeEvent)
			DispatchEvent(Rml::EventId::Change, {{"value", Rml::Variant(v)}});
	}

	void ElemValue::setMinValue(const float _value)
	{
		if (!setProperty(m_min, _value, onMinValueChanged))
			return;
		SetAttribute(g_attribMinValue, _value);
		onChangeMinValue();
		setValue(getValue());	// update value if it is out of range
	}

	void ElemValue::setMaxValue(const float _value)
	{
		if (!setProperty(m_max, _value, onMaxValueChanged))
			return;
		SetAttribute(g_attribMaxValue, _value);
		onChangeMaxValue();
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
