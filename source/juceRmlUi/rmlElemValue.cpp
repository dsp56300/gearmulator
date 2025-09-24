#include "rmlElemValue.h"

#include "juceRmlComponent.h"

#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace juceRmlUi
{
	namespace
	{
		constexpr auto g_attribValue = "value";
		constexpr auto g_attribMinValue = "min";
		constexpr auto g_attribMaxValue = "max";
		constexpr auto g_attribDefaultValue = "default";
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
		checkAttribute(g_attribDefaultValue, [&](const float _x) { setDefaultValue(_x); });
	}

	void ElemValue::setValue(const float _value, bool _sendChangeEvent/* = true*/)
	{
		if (_value == UninitializedValue)
			return;

		const auto v = m_max > m_min ? std::clamp(_value, m_min, m_max) : m_min;

		if (!setProperty(m_value, v, onValueChanged))
			return;

		SetAttribute(g_attribValue, v);
		onChangeValue();
		if (_sendChangeEvent)
			DispatchEvent(Rml::EventId::Change, {{"value", Rml::Variant(v)}});

		RmlComponent::requestUpdate(this);
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

	void ElemValue::setDefaultValue(float _value)
	{
		if (!setProperty(m_default, _value, onDefaultValueChanged))
			return;
		SetAttribute(g_attribDefaultValue, _value);
		onChangeDefaultValue();
	}

	bool ElemValue::setProperty(float& _prop, const float _newValue, const baseLib::Event<float>& _event)
	{
		if (_prop == _newValue)  // NOLINT(clang-diagnostic-float-equal)
			return false;
		_prop = _newValue;
		_event(_newValue);
		return true;
	}

	float ElemValue::getValue(const Rml::Element* _elem)
	{
		if (auto* e = dynamic_cast<const ElemValue*>(_elem))
			return e->getValue();
		if (auto* input = dynamic_cast<const Rml::ElementFormControlInput*>(_elem))
			return Rml::Variant(input->GetValue()).Get<float>(_elem->GetCoreInstance());
		return _elem->GetAttribute<float>("value", 0.0f);
	}

	void ElemValue::setValue(Rml::Element* _elem, const float _value, const bool _sendChangeEvent)
	{
		if (auto* e = dynamic_cast<ElemValue*>(_elem))
			e->setValue(_value, _sendChangeEvent);
		else if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(_elem))
			input->SetValue(std::to_string(_value));
		else
			_elem->SetAttribute("value", _value);
	}

	void ElemValue::setRange(Rml::Element* _volume, const float _min, const float _max)
	{
		if (auto* e = dynamic_cast<ElemValue*>(_volume))
		{
			e->setMinValue(_min);
			e->setMaxValue(_max);
		}
		else
		{
			_volume->SetAttribute("min", _min);
			_volume->SetAttribute("max", _max);
		}
	}
}
