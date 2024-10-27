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
		using PartFormatter = std::function<juce::String(uint8_t, bool)>;	// part, non-part-exclusive

		enum class Origin
		{
			Unknown,
			PresetChange,
			Midi,
			HostAutomation,
			Ui,
			Derived
		};

		Event<Parameter*, bool> onLockedChanged;
		Event<Parameter*, ParameterLinkType> onLinkStateChanged;
		Event<Parameter*> onValueChanged;

		Parameter(Controller& _controller, const Description& _desc, uint8_t _partNum, int _uniqueId, const PartFormatter& _partFormatter);

        juce::Value& getValueObject() { return m_value; }

        const Description& getDescription() const { return m_desc; }

		uint8_t getPart() const { return m_part; }

		const juce::NormalisableRange<float> &getNormalisableRange() const override { return m_range; }

		bool isMetaParameter() const override;

		ParamValue getUnnormalizedValue() const { return juce::roundToInt(m_value.getValue()); }
		float getValue() const override { return convertTo0to1(m_value.getValue()); }

		void setValue(float _newValue) override;

		void setUnnormalizedValue(int _newValue, Origin _origin);
		void setValueNotifyingHost(float _value, Origin _origin);
		void setUnnormalizedValueNotifyingHost(float _value, Origin _origin);
		void setUnnormalizedValueNotifyingHost(int _value, Origin _origin);

		void setValueFromSynth(int _newValue, Origin _origin);

		bool isDiscrete() const override { return m_desc.isDiscrete; }
		bool isBoolean() const override { return m_desc.isBool; }
		bool isBipolar() const { return m_desc.isBipolar; }

		float getValueForText(const juce::String& _text) const override;

		float getDefaultValue() const override
		{
			return convertTo0to1(static_cast<float>(getDefault()));
		}

		virtual ParamValue getDefault() const;

		juce::String getText(float _normalisedValue, int /*maximumStringLength*/) const override;

		void setLocked(bool _locked);
		bool isLocked() const { return m_isLocked; }

		void addDerivedParameter(Parameter* _param);

		int getUniqueId() const { return m_uniqueId; }

		const std::set<Parameter*>& getDerivedParameters() { return m_derivedParameters; }

		Origin getChangeOrigin() const { return m_lastValueOrigin; }

		void setRateLimitMilliseconds(uint32_t _ms);

		void setLinkState(ParameterLinkType _type);
		void clearLinkState(ParameterLinkType _type);

		ParameterLinkType getLinkState() const { return m_linkType; }

		void pushChangeGesture();
		void popChangeGesture();

	private:

		struct ScopedChangeGesture
		{
			explicit ScopedChangeGesture(Parameter& _p);
			~ScopedChangeGesture();

		private:
			Parameter& m_parameter;
		};

        static juce::String genId(const Description &d, int part, int uniqueId);
		void valueChanged(juce::Value &) override;
		void setDerivedValue(const int _value);
		void sendToSynth();
		static uint64_t milliseconds();
		void sendParameterChangeDelayed(ParamValue _value, uint32_t _uniqueId);
		void forwardToDerived(const int _newValue);
		void notifyHost(float _value);

		int clampValue(int _value) const;

        Controller& m_controller;
		const Description m_desc;
		juce::NormalisableRange<float> m_range;
		const uint8_t m_part;
		const int m_uniqueId;	// 0 for all unique parameters, > 0 if multiple Parameter instances reference a single synth parameter

		int m_lastValue{-1};
		Origin m_lastValueOrigin = Origin::Unknown;
		juce::Value m_value;
		std::set<Parameter*> m_derivedParameters;
		bool m_changingDerivedValues = false;

		uint32_t m_rateLimit = 0;		// milliseconds
		uint64_t m_lastSendTime = 0;
		uint32_t m_uniqueDelayCallbackId = 0;

		bool m_isLocked = false;
		ParameterLinkType m_linkType = None;
		uint32_t m_changeGestureCount = 0;
		bool m_notifyingHost = false;
    };
}
