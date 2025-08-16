#include "rmlElemButton.h"

#include "rmlEventListener.h"

namespace juceRmlUi
{
	ElemButton::ElemButton(Rml::CoreInstance& _coreInstance, const Rml::String& _tag): ElemValue(_coreInstance, _tag)
	{
		EventListener::Add(this, Rml::EventId::Click, [&](const Rml::Event& _event) { onClick(); });
		EventListener::Add(this, Rml::EventId::Mousedown, [&](const Rml::Event& _event) { onMouseDown(); });
		EventListener::Add(this, Rml::EventId::Mouseup, [&](const Rml::Event& _event) { onMouseUp(); });
	}

	void ElemButton::onChangeValue()
	{
		ElemValue::onChangeValue();

		const auto v = static_cast<pluginLib::ParamValue>(getValue());
		setChecked(v == getValueOn());
	}

	void ElemButton::onPropertyChanged(const std::string& _key)
	{
		ElemValue::onPropertyChanged(_key);

		if (_key == "isToggle")			m_isToggle = getProperty<bool>("isToggle", false);
		else if (_key == "valueOn")		m_valueOn  = getProperty<int>("valueOn", -1);
		else if (_key == "valueOff")	m_valueOff = getProperty<int>("valueOff", -1);
	}

	void ElemButton::OnChildAdd(Rml::Element* _child)
	{
		ElemValue::OnChildAdd(_child);

		if (_child->GetTagName() == "buttonhittest")
			m_hitTestElem = _child;
	}

	void ElemButton::OnChildRemove(Rml::Element* _child)
	{
		ElemValue::OnChildRemove(_child);

		if (_child == m_hitTestElem)
			m_hitTestElem = nullptr;
	}

	bool ElemButton::IsPointWithinElement(const Rml::Vector2f _point)
	{
		if (m_hitTestElem)
			return m_hitTestElem->IsPointWithinElement(_point);
		return ElemValue::IsPointWithinElement(_point);
	}

	void ElemButton::onClick()
	{
		if (m_isToggle)
			setChecked(!getChecked());

		evClick(this);
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

	pluginLib::ParamValue ElemButton::getValueOn() const
	{
		return m_valueOn == -1 && m_valueOff == -1 ? 1 : m_valueOn;
	}

	pluginLib::ParamValue ElemButton::getValueOff() const
	{
		return m_valueOn == -1 && m_valueOff == -1 ? 0 : m_valueOff;
	}

	void ElemButton::setChecked(const bool _checked)
	{
		if (m_isChecked == _checked)
			return;

		m_isChecked = _checked;

		if (_checked && getValueOn() >= 0)
			setValue(static_cast<float>(getValueOn()));
		else if (!_checked && getValueOff() >= 0)
			setValue(static_cast<float>(getValueOff()));

		SetPseudoClass("checked", _checked);
	}
}
