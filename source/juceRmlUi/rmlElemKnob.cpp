#include "rmlElemKnob.h"

#include "rmlHelper.h"

namespace juceRmlUi
{
	ElemKnob::ElemKnob(Rml::CoreInstance& _coreInstance, const Rml::String& _tag): ElemValue(_coreInstance, _tag)
	{
		AddEventListener(Rml::EventId::Mousedown, this);
		AddEventListener(Rml::EventId::Drag, this);
		AddEventListener(Rml::EventId::Mousescroll, this);

		auto setSpriteDirty = [this](const float&)
		{
			m_spriteDirty = true;
		};

		m_onValueChanged.set(onValueChanged, setSpriteDirty);
		m_onMinValueChanged.set(onValueChanged, setSpriteDirty);
		m_onMaxValueChanged.set(onValueChanged, setSpriteDirty);
	}

	ElemKnob::~ElemKnob()
	{
		RemoveEventListener(Rml::EventId::Mousedown, this);
		RemoveEventListener(Rml::EventId::Drag, this);
		RemoveEventListener(Rml::EventId::Mousescroll, this);
	}

	void ElemKnob::onPropertyChanged(const std::string& _key)
	{
		ElemValue::onPropertyChanged(_key);

		if (_key == "spriteprefix")
		{
			m_spritesheetPrefix = getProperty<Rml::String>("spriteprefix", "");
			m_spriteDirty = true;
		}
		else if (_key == "speed")
		{
			m_speed = getProperty<float>("speed", m_speed);
		}
		else if (_key == "frames")
		{
			m_frames = getProperty<uint32_t>("frames", m_frames);
			m_spriteDirty = true;
		}
	}

	void ElemKnob::OnUpdate()
	{
		if (m_spriteDirty)
			updateSprite();
		ElemValue::OnUpdate();
	}

	void ElemKnob::ProcessEvent(Rml::Event& _event)
	{
		switch (_event.GetId())
		{
		case Rml::EventId::Mousedown:
			m_lastMousePos = helper::getMousePos(_event);
			m_mouseDownValue = getValue();
			break;
		case Rml::EventId::Drag:
			processMouseMove(helper::getMousePos(_event) - m_lastMousePos);
			break;
		case Rml::EventId::Mousescroll:
			processMouseWheel(_event);
			break;
		}
	}

	void ElemKnob::processMouseMove(const Rml::Vector2f& _delta)
	{
		const auto delta = (_delta.x - _delta.y) * m_speed;

		const auto value = m_mouseDownValue + delta;

		setValue(value);
	}

	void ElemKnob::processMouseWheel(const Rml::Event& _event)
	{
		const auto wheel = helper::getMouseWheelDelta(_event);
		const auto delta = wheel.y * m_speed;

		const auto range = getMaxValue() - getMinValue();

		// we use the default behaviour if ctrl/cmd is not pressed and the range is large enough
		if(range > 32 && !helper::getKeyModCtrl(_event))
		{
			setValue(getValue() - range * delta / 7.5f);	// this should be pretty close to what Juce did
			return;
		}

		// Otherwise inc/dec single steps

		constexpr auto diff = 1;

		if(delta > 0)
			setValue(getValue() - diff);
		else
			setValue(getValue() + diff);
	}

	void ElemKnob::updateSprite()
	{
		if (m_spritesheetPrefix.empty() || !m_frames)
			return;

		const auto min = getMinValue();
		const auto max = getMaxValue();

		if (max <= min)
			return;

		const auto value = std::clamp(getValue(), min, max);

		const auto percent = max > min ? (value - min) / (max - min) : 0;
		const auto frame = static_cast<uint32_t>(percent * static_cast<float>(m_frames - 1));

		char frameAsString[32];
		(void)snprintf(frameAsString, sizeof(frameAsString), "%03u", frame);

//		const auto currentSprite = GetAttribute<Rml::String>("sprite", "");
		const auto newSprite = m_spritesheetPrefix + frameAsString;

//		if (newSprite != currentSprite)
			SetProperty("decorator", "image(" + newSprite + " contain)");

		m_spriteDirty = false;
	}
}
