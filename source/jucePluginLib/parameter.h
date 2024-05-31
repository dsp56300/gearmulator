#pragma once

#include <set>

#include "parameterdescription.h"

#include "juce_audio_processors/juce_audio_processors.h"

#include "event.h"
#include "types.h"

namespace pluginLib
{
	class Controller;

	class Parameter : juce::Value::Listener, public juce::RangedAudioParameter
	{
    public:
		enum class ChangedBy
		{
			Unknown,
			PresetChange,
			ControlChange,
			HostAutomation,
			Ui,
			Derived
		};

		Event<Parameter*, bool> onLockedChanged;
		Event<Parameter*, ParameterLinkType> onLinkStateChanged;
		Event<Parameter*> onValueChanged;

		Parameter(Controller& _controller, const Description& _desc, uint8_t _partNum, int _uniqueId);

        juce::Value &getValueObject() { return m_value; }
        const juce::Value &getValueObject() const { return m_value; }

        const Description& getDescription() const { return m_desc; }

		uint8_t getPart() const { return m_partNum; }

		const juce::NormalisableRange<float> &getNormalisableRange() const override { return m_range; }

		bool isMetaParameter() const override;

		float getValue() const override { return convertTo0to1(m_value.getValue()); }
		int getUnnormalizedValue() const { return juce::roundToInt(m_value.getValue()); }
		void setValue(float _newValue) override;
		void setValue(float _newValue, ChangedBy _origin);
		void setUnnormalizedValue(int _newValue, ChangedBy _origin);
		void setValueFromSynth(int newValue, bool notifyHost, ChangedBy _origin);

		bool isDiscrete() const override { return m_desc.isDiscrete; }
		bool isBoolean() const override { return m_desc.isBool; }
		bool isBipolar() const { return m_desc.isBipolar; }

		float getValueForText(const juce::String &text) const override
		{
			const auto res = m_desc.valueList.textToValue(std::string(text.getCharPointer()));
			return convertTo0to1(static_cast<float>(res));
		}

		float getDefaultValue() const override
		{
			return convertTo0to1((float)getDefault());
		}

		virtual uint8_t getDefault() const;

		juce::String getText(float normalisedValue, int /*maximumStringLength*/) const override
		{
			const auto v = convertFrom0to1(normalisedValue);
			return m_desc.valueList.valueToText(juce::roundToInt(v));
		}

		void setLocked(bool _locked);
		bool isLocked() const { return m_isLocked; }

		void addDerivedParameter(Parameter* _param);

		int getUniqueId() const { return m_uniqueId; }

		const std::set<Parameter*>& getDerivedParameters() { return m_derivedParameters; }

		ChangedBy getChangeOrigin() const { return m_lastValueOrigin; }

		void setValueNotifyingHost(float _value, ChangedBy _origin);

		void setRateLimitMilliseconds(uint32_t _ms);

		void setLinkState(ParameterLinkType _type);
		void clearLinkState(ParameterLinkType _type);

		ParameterLinkType getLinkState() const { return m_linkType; }

	private:
        static juce::String genId(const Description &d, int part, int uniqueId);
		void valueChanged(juce::Value &) override;
		void setDerivedValue(int _value, ChangedBy _origin, bool _notifyHost);
		void sendToSynth();
		static uint64_t milliseconds();
		void sendParameterChangeDelayed(uint8_t, uint32_t _uniqueId);

        Controller& m_ctrl;
		const Description m_desc;
		juce::NormalisableRange<float> m_range;
		const uint8_t m_partNum;
		const int m_uniqueId;	// 0 for all unique parameters, > 0 if multiple Parameter instances reference a single synth parameter
		int m_lastValue{-1};
		ChangedBy m_lastValueOrigin = ChangedBy::Unknown;
		juce::Value m_value;
		std::set<Parameter*> m_derivedParameters;
		bool m_changingDerivedValues = false;

		uint32_t m_rateLimit = 0;		// milliseconds
		uint64_t m_lastSendTime = 0;
		uint32_t m_uniqueDelayCallbackId = 0;
		bool m_isLocked = false;
		ParameterLinkType m_linkType = None;
    };
}
