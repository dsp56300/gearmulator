#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Virus
{
    class Controller;

	class Parameter : juce::Value::Listener, public juce::RangedAudioParameter
	{
    public:
        enum Class
        {
            UNDEFINED = 0x0,
            GLOBAL = 0x1,
            PERFORMANCE_CONTROLLER = 0x2,
            SOUNDBANK_A = 0x4,
            SOUNDBANK_B = 0x8,
            MULTI_OR_SINGLE = 0x10,
            MULTI_PARAM = 0x20,
            NON_PART_SENSITIVE = 0x40,
            BANK_PROGRAM_CHANGE_PARAM_BANK_SELECT = 0x80,
            VIRUS_C = 0x100,
        };

        enum Page
        {
            A = 0x70,
            B = 0x71,
            C = 0x72,
			D = 0x73
        };

        struct Description
        {
            Page page;
            int classFlags;
            uint8_t index;
            juce::String name;
            juce::Range<int> range;
			std::function<juce::String(float, Description ctx)> valueToTextFunction;
			std::function<float(const juce::String &, Description ctx)> textToValueFunction;
			bool isPublic;
			bool isDiscrete;
			bool isBool;
			bool isBipolar;
        };

        Parameter(Controller &, const Description desc, uint8_t partNum = 0x40);

        juce::Value &getValueObject() { return m_value; };
        const juce::Value &getValueObject() const { return m_value; };

        const Description getDescription() const { return m_desc; };

		const juce::NormalisableRange<float> &getNormalisableRange() const override { return m_range; }

		float getValue() const override { return convertTo0to1(m_value.getValue()); }
		void setValue(float newValue) override { return m_value.setValue(convertFrom0to1(newValue)); };
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
			if (m_desc.textToValueFunction)
				return convertTo0to1(m_desc.textToValueFunction(text, m_desc));
			if (m_desc.valueToTextFunction)
			{
				// brute force but this should be O(1) of 128...
				for (auto i = 0; i < 128; i++)
					if (m_desc.valueToTextFunction(static_cast<float>(i), m_desc) == text)
						return convertTo0to1(static_cast<float>(i));
			}
			return convertTo0to1(text.getFloatValue());
		}

		juce::String getText(float normalisedValue, int /*maximumStringLength*/) const override
		{
			if (m_desc.valueToTextFunction)
				return m_desc.valueToTextFunction(convertFrom0to1(normalisedValue), m_desc);
			else
				return juce::String(juce::roundToInt(convertFrom0to1(normalisedValue)));
		}

		// allow 'injecting' additional code for specific parameter.
		// eg. multi/single value change requires triggering more logic.
		std::function<void()> onValueChanged = {};

	private:
		juce::String genId(const Description &d, int part);
		void valueChanged(juce::Value &) override;

        Controller &m_ctrl;
		const Description m_desc;
		juce::NormalisableRange<float> m_range;
		uint8_t m_paramNum, m_partNum;
		int m_lastValue{-1};
		juce::Value m_value;
    };
} // namespace Virus
