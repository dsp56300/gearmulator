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
		void processMouseMove(const Rml::Vector2f& _delta);
		void processMouseWheel(const Rml::Event& _event);

		void updateSprite();

		Rml::Vector2<float> m_lastMousePos;
		Rml::String m_spritesheetPrefix;
		float m_speed = 1.0f;
		uint32_t m_frames = 16;
		bool m_spriteDirty = true;
		float m_mouseDownValue = 0.0f;

		baseLib::EventListener<float> m_onValueChanged;
		baseLib::EventListener<float> m_onMinValueChanged;
		baseLib::EventListener<float> m_onMaxValueChanged;
	};
}
