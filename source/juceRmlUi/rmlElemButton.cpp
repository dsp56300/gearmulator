#include "rmlElemButton.h"

#include "rmlEventListener.h"

namespace juceRmlUi
{
	ElemButton::ElemButton(const Rml::String& _tag): ElemValue(_tag)
	{
		EventListener::Add(this, Rml::EventId::Click, [&](const Rml::Event& _event) { onClick(); });
		EventListener::Add(this, Rml::EventId::Mousedown, [&](const Rml::Event& _event) { onMouseDown(); });
		EventListener::Add(this, Rml::EventId::Mouseup, [&](const Rml::Event& _event) { onMouseUp(); });
	}

	void ElemButton::onChangeValue()
	{
		ElemValue::onChangeValue();

		const auto v = static_cast<pluginLib::ParamValue>(getValue());
		setChecked(v == m_valueOn);
	}

	void ElemButton::onPropertyChanged(const std::string& _key)
	{
		ElemValue::onPropertyChanged(_key);

		if (_key == "isToggle")			m_isToggle = getProperty<bool>("isToggle", false);
		else if (_key == "valueOn")		m_valueOn  = getProperty<int>("valueOn", -1);
		else if (_key == "valueOff")	m_valueOff = getProperty<int>("valueOff", -1);
	}

	void ElemButton::onClick()
	{
		if (m_isToggle)
			setChecked(!getChecked());
	}

	void ElemButton::onMouseDown()
	{
		if (!m_isToggle)
			setChecked(true);
	}

	void ElemButton::onMouseUp()
	{
		if (!m_isToggle)
			setChecked(false);
	}

	void ElemButton::setChecked(const bool _checked)
	{
		if (m_isChecked == _checked)
			return;

		m_isChecked = _checked;

		if (_checked && m_valueOn >= 0)
			setValue(static_cast<float>(m_valueOn));
		else if (!_checked && m_valueOff >= 0)
			setValue(static_cast<float>(m_valueOff));

		SetPseudoClass("checked", _checked);
	}
}
