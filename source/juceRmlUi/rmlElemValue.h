#pragma once

#include "RmlUi/Core/Element.h"

#include "baseLib/event.h"

namespace juceRmlUi
{
	class ElemValue : public Rml::Element
	{
	public:
		baseLib::Event<float> onValueChanged;
		baseLib::Event<float> onMinValueChanged;
		baseLib::Event<float> onMaxValueChanged;

		explicit ElemValue(const Rml::String& _tag) : Rml::Element(_tag) {}

		void OnAttributeChange(const Rml::ElementAttributes& _changedAttributes) override;

		void setValue(const float _value);
		void setMinValue(const float _value);
		void setMaxValue(const float _value);

		float getValue() const { return m_value; }
		float getMinValue() const { return m_min; }
		float getMaxValue() const { return m_max; }

		static bool setProperty(float& _prop, float _newValue, const baseLib::Event<float>& _event);

	private:
		float m_value = 0.0f;
		float m_min = 0.0f;
		float m_max = 1.0f;
	};
}
