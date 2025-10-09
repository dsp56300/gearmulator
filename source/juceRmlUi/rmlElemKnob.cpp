#include "rmlElemKnob.h"

#include "rmlHelper.h"

namespace juceRmlUi
{
	ElemKnob::ElemKnob(Rml::CoreInstance& _coreInstance, const Rml::String& _tag): ElemValue(_coreInstance, _tag)
	{
		AddEventListener(Rml::EventId::Mousedown, this);
		AddEventListener(Rml::EventId::Drag, this);
		AddEventListener(Rml::EventId::Mousescroll, this);
		AddEventListener(Rml::EventId::Dblclick, this);

		auto setSpriteDirty = [this](const float&)
		{
			m_spriteDirty = true;
		};

		m_onValueChanged.set(onValueChanged, setSpriteDirty);
		m_onMinValueChanged.set(onMinValueChanged, setSpriteDirty);
		m_onMaxValueChanged.set(onMaxValueChanged, setSpriteDirty);
	}

	ElemKnob::~ElemKnob()
	{
		RemoveEventListener(Rml::EventId::Mousedown, this);
		RemoveEventListener(Rml::EventId::Drag, this);
		RemoveEventListener(Rml::EventId::Mousescroll, this);
		RemoveEventListener(Rml::EventId::Dblclick, this);
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
		else if (_key == "speedScaleShift")
		{
			m_speedScaleShift = getProperty<float>("speedScaleShift", m_speedScaleShift);
		}
		else if (_key == "speedScaleCtrl")
		{
			m_speedScaleCtrl = getProperty<float>("speedScaleCtrl", m_speedScaleCtrl);
		}
		else if (_key == "speedScaleAlt")
		{
			m_speedScaleAlt = getProperty<float>("speedScaleAlt", m_speedScaleAlt);
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
			processMouseMove(_event);
			break;
		case Rml::EventId::Mousescroll:
			processMouseWheel(_event);
			break;
		case Rml::EventId::Dblclick:
			processDoubleClick(_event);
			break;
		}
	}

	void ElemKnob::setEndless(bool _endless)
	{
		m_endless = _endless;
	}

	bool ElemKnob::isReversed(const Rml::Element* _element)
	{
		auto* attrib = _element->GetAttribute("orientation");
		if (!attrib)
			return false;
		return attrib->Get(_element->GetCoreInstance(), std::string()) == "vertical";
	}

	void ElemKnob::processMouseWheel(Rml::Element& _element, const Rml::Event& _event)
	{
		const auto wheel = helper::getMouseWheelDelta(_event);
		auto delta = wheel.y;

		if (isReversed(&_element))
			delta = -delta;

		const auto range = getRange(&_element);

		// we use the default behaviour if ctrl/cmd is not pressed and the range is large enough
		if(range > 32 && !helper::getKeyModCommand(_event))
		{
			setValue(&_element, getValue(&_element) - range * delta / 7.5f);	// this should be pretty close to what Juce did
			return;
		}

		// Otherwise inc/dec single steps

		constexpr auto diff = 1;

		if(delta > 0)
			setValue(&_element, getValue(&_element) - diff);
		else
			setValue(&_element, getValue(&_element) + diff);
	}

	void ElemKnob::processMouseMove(const Rml::Event& _event)
	{
		const auto range = getRange();

		if (range <= 0)
			return;

		float mod = 1.0f;

		if (helper::getKeyModShift(_event))
			mod = m_speedScaleShift;
		else if (helper::getKeyModCommand(_event))
			mod = m_speedScaleCtrl;
		else if (helper::getKeyModAlt(_event))
			mod = m_speedScaleAlt;

		if (mod != m_lastMod)
		{
			m_mouseDownValue = getValue();
			m_lastMod = mod;
			m_lastMousePos = helper::getMousePos(_event);
		}

		const auto delta = helper::getMousePos(_event) - m_lastMousePos;

		const auto d = (delta.x - delta.y) * range * mod / m_speed;

		auto value = m_mouseDownValue + d;

		if (m_endless)
		{
			while (value > getMaxValue())
				value -= range;
			while (value < getMinValue())
				value += range;
		}

		setValue(value);
	}

	void ElemKnob::processMouseWheel(const Rml::Event& _event)
	{
		processMouseWheel(*this, _event);
	}

	void ElemKnob::processDoubleClick(const Rml::Event&)
	{
		const auto d = getDefaultValue();
		if (isInRange(d))
			setValue(d);
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
