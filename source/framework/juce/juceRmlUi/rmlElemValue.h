#pragma once

#include "rmlElement.h"

#include "baseLib/event.h"

namespace juceRmlUi
{
	class ElemValue : public Element
	{
	public:
		baseLib::Event<float> onValueChanged;
		baseLib::Event<float> onMinValueChanged;
		baseLib::Event<float> onMaxValueChanged;
		baseLib::Event<float> onDefaultValueChanged;
		baseLib::Event<float> onStepSizeChanged;

		static constexpr float UninitializedValue = std::numeric_limits<float>::lowest();

		explicit ElemValue(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : Element(_coreInstance, _tag) {}

		void OnAttributeChange(const Rml::ElementAttributes& _changedAttributes) override;

		void setValue(float _value, bool _sendChangeEvent = true);
		void setMinValue(float _value);
		void setMaxValue(float _value);
		void setDefaultValue(float _value);
		void setStepSize(float _value);

		float getValue() const { return m_value; }
		float getMinValue() const { return m_min; }
		float getMaxValue() const { return m_max; }
		float getDefaultValue() const { return m_default; }

		float getRange() const
		{
			if (m_max <= m_min)
				return 0.0f;
			return m_max - m_min;
		}

		static float getRange(Rml::Element* _elem);

		bool isInRange(const float _value) const
		{
			return _value >= m_min && _value <= m_max;
		}

		virtual void onChangeValue() {}
		virtual void onChangeMinValue() {}
		virtual void onChangeMaxValue() {}
		virtual void onChangeDefaultValue() {}
		virtual void onChangeStepSize() {}

		static bool setProperty(float& _prop, float _newValue, const baseLib::Event<float>& _event);

		static float getValue(const Rml::Element* _elem, float _default = 0.0f);
		static void setValue(Rml::Element* _elem, float _value, bool _sendChangeEvent = true);

		static void setRange(Rml::Element* _elem, float _min, float _max);

	private:
		float m_value = UninitializedValue;
		float m_min = 0.0f;
		float m_max = 1.0f;
		float m_default = 0.5f;
		float m_stepSize = 0.0f;
	};
}
