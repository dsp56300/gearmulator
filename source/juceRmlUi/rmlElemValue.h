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

		explicit ElemValue(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : Element(_coreInstance, _tag) {}

		void OnAttributeChange(const Rml::ElementAttributes& _changedAttributes) override;

		void setValue(float _value, bool _sendChangeEvent = true);
		void setMinValue(float _value);
		void setMaxValue(float _value);

		float getValue() const { return m_value; }
		float getMinValue() const { return m_min; }
		float getMaxValue() const { return m_max; }

		float getRange() const
		{
			if (m_max <= m_min)
				return 0.0f;
			return m_max - m_min;
		}

		virtual void onChangeValue() {}
		virtual void onChangeMinValue() {}
		virtual void onChangeMaxValue() {}

		static bool setProperty(float& _prop, float _newValue, const baseLib::Event<float>& _event);

	private:
		float m_value = -1.0f;
		float m_min = 0.0f;
		float m_max = 1.0f;
	};
}
