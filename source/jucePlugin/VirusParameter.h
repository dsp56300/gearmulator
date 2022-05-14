#pragma once

#include <set>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../jucePluginLib/parameterdescription.h"

namespace Virus
{
	class Controller;

	class Parameter : juce::Value::Listener, public juce::RangedAudioParameter
	{
    public:
		Parameter(Controller &, const pluginLib::Description& desc, uint8_t partNum, int uniqueId);

        juce::Value &getValueObject() { return m_value; };
        const juce::Value &getValueObject() const { return m_value; };

        const pluginLib::Description& getDescription() const { return m_desc; };

		const juce::NormalisableRange<float> &getNormalisableRange() const override { return m_range; }

		bool isMetaParameter() const override;

		float getValue() const override { return convertTo0to1(m_value.getValue()); }
		void setValue(float newValue) override;
		void setValueFromSynth(int newValue, bool notifyHost = true);
		float getDefaultValue() const override {
			// default value should probably be in description instead of hardcoded like this.
			if (m_desc.index == 21 || m_desc.index == 31) // osc keyfollows
				return (64.0f + 32.0f) / 127.0f;
			if (m_desc.index == 36) // osc vol / saturation
				return 0.5f;
			if (m_desc.index == 40) // filter1 cutoffs
				return 1.0f;
			if(m_desc.index == 41) //filter 2
				return 0.5f;
			if(m_desc.index == 91) // patch volume
				return 100.0f / 127.0f;
			return m_desc.isBipolar ? 0.5f : 0.0f; /* maybe return from ROM state? */
		}
		bool isDiscrete() const override { return m_desc.isDiscrete; }
		bool isBoolean() const override { return m_desc.isBool; }
		bool isBipolar() const { return m_desc.isBipolar; }

		float getValueForText(const juce::String &text) const override
		{
			const auto res = m_desc.valueList.textToValue(std::string(text.getCharPointer()));
			return convertTo0to1(static_cast<float>(res));
		}

		juce::String getText(float normalisedValue, int /*maximumStringLength*/) const override
		{
			const auto v = convertFrom0to1(normalisedValue);
			return m_desc.valueList.valueToText(juce::roundToInt(v));
		}

		// allow 'injecting' additional code for specific parameter.
		// eg. multi/single value change requires triggering more logic.
		std::function<void()> onValueChanged = {};

		void addLinkedParameter(Parameter* _param);

		int getUniqueId() const { return m_uniqueId; }

	private:
        static juce::String genId(const pluginLib::Description &d, int part, int uniqueId);
		void valueChanged(juce::Value &) override;
		void setLinkedValue(int _value);

        Controller &m_ctrl;
		const pluginLib::Description m_desc;
		juce::NormalisableRange<float> m_range;
		const uint8_t m_partNum;
		const int m_uniqueId;	// 0 for all unique parameters, > 0 if multiple Parameter instances reference a single synth parameter
		int m_lastValue{-1};
		juce::Value m_value;
		std::set<Parameter*> m_linkedParameters;
		bool m_changingLinkedValues = false;
    };
} // namespace Virus
