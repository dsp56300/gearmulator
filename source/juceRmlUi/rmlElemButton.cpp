#include "rmlElemButton.h"

#include "juceRmlComponent.h"
#include "rmlEventListener.h"

namespace juceRmlUi
{
	ElemButton::ElemButton(Rml::CoreInstance& _coreInstance, const Rml::String& _tag): ElemValue(_coreInstance, _tag)
	{
		EventListener::Add(this, Rml::EventId::Click, [&](const Rml::Event&) { onClick(); });
		EventListener::Add(this, Rml::EventId::Mousedown, [&](const Rml::Event&) { onMouseDown(); });
		EventListener::Add(this, Rml::EventId::Mouseup, [&](const Rml::Event&) { onMouseUp(); });

		// TODO: mouse out while pressed? Should removed the pressed state
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

		if (_key == "isToggle")
		{
			m_isToggle = getProperty<bool>("isToggle", false);
		}
		else if (_key == "valueOn")
		{
			m_valueOn = getProperty<int>("valueOn", -1);
			setChecked(static_cast<pluginLib::ParamValue>(getValue()) == getValueOn());
		}
		else if (_key == "valueOff")
		{
			m_valueOff = getProperty<int>("valueOff", -1);
		}
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

	bool ElemButton::isToggle(const Rml::Element* _button)
	{
		if (auto* elemButton = dynamic_cast<const ElemButton*>(_button))
			return elemButton->isToggle();
		return false;
	}

	void ElemButton::onClick()
	{
		if (m_isToggle)
		{
			if (getChecked() && getValueOn() >= 0 && getValueOff() < 0)
			{
				// if this toggle has positive values only, don't allow unchecking
			}
			else
			{
				setChecked(!getChecked());
			}
		}

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

		if (auto* comp = RmlComponent::fromElement(this))
			comp->enqueueUpdate();
	}

	void ElemButton::setChecked(Rml::Element* _button, bool _checked)
	{
		if (auto* elemButton = dynamic_cast<ElemButton*>(_button))
			elemButton->setChecked(_checked);
		else
			_button->SetPseudoClass("checked", _checked);
	}

	bool ElemButton::isChecked(const Rml::Element* _button)
	{
		if (auto* elemButton = dynamic_cast<const ElemButton*>(_button))
			return elemButton->isChecked();
		return _button->IsPseudoClassSet("checked");
	}

	bool ElemButton::isPressed(const Rml::Element* _element)
	{
		if (auto* elemButton = dynamic_cast<const ElemButton*>(_element))
			return elemButton->isPressed();
		return _element->IsPseudoClassSet("pressed");
	}

	bool ElemButton::isPressed() const
	{
		// FIXME: non-toggles get the "checked" state while pressed, we should add a separate "pressed" state
		return m_isChecked;
	}
}
