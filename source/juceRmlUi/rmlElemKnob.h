#pragma once

#include "rmlElemValue.h"

#include "RmlUi/Core/ElementInstancer.h"
#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class ElemKnob : public ElemValue, public Rml::EventListener
	{
	public:
		explicit ElemKnob(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);
		~ElemKnob() override;

		void onPropertyChanged(const std::string& _key) override;

		void OnUpdate() override;

		void ProcessEvent(Rml::Event& _event) override;

	private:
		void processMouseMove(const Rml::Event& _event);
		void processMouseWheel(const Rml::Event& _event);

		void updateSprite();

		Rml::Vector2<float> m_lastMousePos;
		Rml::String m_spritesheetPrefix;
		float m_speed = 150.0f; /* what mouse delta is needed to do a full sweep from 0% to 100% */
		uint32_t m_frames = 16;
		bool m_spriteDirty = true;
		float m_mouseDownValue = 0.0f;

		float m_speedScaleShift = 0.1f;
		float m_speedScaleCtrl = 0.2f;
		float m_speedScaleAlt = 0.5f;

		float m_lastMod = -1.0f;

		baseLib::EventListener<float> m_onValueChanged;
		baseLib::EventListener<float> m_onMinValueChanged;
		baseLib::EventListener<float> m_onMaxValueChanged;
	};
}
