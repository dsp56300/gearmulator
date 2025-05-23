#include "rmlElemKnob.h"

#include "rmlHelper.h"

namespace juceRmlUi
{
	ElemKnob::ElemKnob(const Rml::String& _tag): ElemValue(_tag)
	{
		AddEventListener(Rml::EventId::Mousedown, this);
		AddEventListener(Rml::EventId::Drag, this);

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
		if (_event.GetId() == Rml::EventId::Mousedown)
		{
			m_lastMousePos = helper::getMousePos(_event);
		}
		else if (_event.GetId() == Rml::EventId::Drag)
		{
			auto mousePos = helper::getMousePos(_event);
			processMouseMove(mousePos - m_lastMousePos);
			m_lastMousePos = mousePos;
		}
	}

	void ElemKnob::processMouseMove(const Rml::Vector2f& _delta)
	{
		const auto delta = (_delta.x - _delta.y) * m_speed;

		const auto value = getValue() + delta;

		setValue(value);
	}

	void ElemKnob::updateSprite()
	{
		if (m_spritesheetPrefix.empty())
			return;

		const auto value = getValue();
		const auto min = getMinValue();
		const auto max = getMaxValue();
		const auto percent = max > min ? (value - min) / (max - min) : 0;
		const auto frame = static_cast<uint32_t>(percent * static_cast<float>(m_frames - 1));

		char frameAsString[16];
		(void)snprintf(frameAsString, sizeof(frameAsString), "%03u", frame);

//		const auto currentSprite = GetAttribute<Rml::String>("sprite", "");
		const auto newSprite = m_spritesheetPrefix + frameAsString;

//		if (newSprite != currentSprite)
			SetProperty("decorator", "image(" + newSprite + ")");

		m_spriteDirty = false;
	}
}
