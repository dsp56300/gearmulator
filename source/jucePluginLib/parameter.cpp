#include "parameter.h"

#include "controller.h"

namespace pluginLib
{
	Parameter::Parameter(Controller& _controller, const Description& _desc, const uint8_t _partNum, const int _uniqueId, const PartFormatter& _partFormatter)
		: juce::RangedAudioParameter(genId(_desc, _partNum, _uniqueId), _partFormatter(_partNum, _desc.isNonPartSensitive()) + " " + _desc.displayName)
		, m_controller(_controller)
		, m_desc(_desc)
		, m_part(_partNum)
		, m_uniqueId(_uniqueId)
	{
		m_range.start = static_cast<float>(m_desc.range.getStart());
		m_range.end = static_cast<float>(m_desc.range.getEnd());
		m_range.interval = m_desc.step ? static_cast<float>(m_desc.step) : (m_desc.isDiscrete || m_desc.isBool ? 1.0f : 0.0f);

		m_value.setValue(m_range.start);
		m_value.addListener(this);
    }

    void Parameter::valueChanged(juce::Value&)
    {
		sendToSynth();
		onValueChanged(this);
	}

    void Parameter::setDerivedValue(const int _value)
    {
		const int newValue = clampValue(_value);

		if (newValue == m_lastValue)
			return;

		m_lastValue = newValue;
		m_lastValueOrigin = Origin::Derived;

		m_value.setValue(newValue);
	}

    void Parameter::sendToSynth()
    {
		const float floatValue = m_value.getValue();
		const auto value = juce::roundToInt(floatValue);

		jassert(m_range.getRange().contains(floatValue) || m_range.end == floatValue);

		if (value == m_lastValue)
			return;

		// ignore initial update
		if (m_lastValue != -1)
		{
			if(m_rateLimit)
			{
				sendParameterChangeDelayed(value, ++m_uniqueDelayCallbackId);
			}
			else
			{
				m_lastSendTime = milliseconds();
				m_controller.sendParameterChange(*this, value);
			}
		}

		m_lastValue = value;
    }

    uint64_t Parameter::milliseconds()
    {
		const auto t = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
		return t.count();
    }

    void Parameter::sendParameterChangeDelayed(const ParamValue _value, uint32_t _uniqueId)
    {
		if(_uniqueId != m_uniqueDelayCallbackId)
			return;

		const auto ms = milliseconds();

		const auto elapsed = ms - m_lastSendTime;
		if(elapsed >= m_rateLimit)
		{
			m_lastSendTime = ms;
			m_controller.sendParameterChange(*this, _value);
		}
		else
		{
			juce::Timer::callAfterDelay(static_cast<int>(elapsed), [this, _value, _uniqueId]
			{
				sendParameterChangeDelayed(_value, _uniqueId);
			});
		}
    }

    int Parameter::clampValue(const int _value) const
    {
		return juce::roundToInt(m_range.getRange().clipValue(static_cast<float>(_value)));
    }

    void Parameter::setValueNotifyingHost(const float _value, const Origin _origin)
    {
		ScopedChangeGesture g(*this);
		setUnnormalizedValue(juce::roundToInt(convertFrom0to1(_value)), _origin);
		notifyHost(_value);
	}

    void Parameter::setUnnormalizedValueNotifyingHost(const float _value, const Origin _origin)
    {
		ScopedChangeGesture g(*this);
		setUnnormalizedValue(juce::roundToInt(_value), _origin);
		notifyHost(convertTo0to1(_value));
    }

    void Parameter::setUnnormalizedValueNotifyingHost(const int _value, const Origin _origin)
    {
		ScopedChangeGesture g(*this);
		setUnnormalizedValue(_value, _origin);
		notifyHost(convertTo0to1(static_cast<float>(_value)));
    }

    void Parameter::setRateLimitMilliseconds(const uint32_t _ms)
    {
	    m_rateLimit = _ms;
    }

    void Parameter::setLinkState(const ParameterLinkType _type)
    {
		const auto prev = m_linkType;
		m_linkType = static_cast<ParameterLinkType>(m_linkType | _type);
		if(m_linkType != prev)
			onLinkStateChanged(this, m_linkType);
    }

    void Parameter::clearLinkState(const ParameterLinkType _type)
    {
		const auto prev = m_linkType;
		m_linkType = static_cast<ParameterLinkType>(m_linkType & ~_type);
		if(m_linkType != prev)
			onLinkStateChanged(this, m_linkType);
    }

    void Parameter::pushChangeGesture()
    {
		if(!m_changeGestureCount)
			beginChangeGesture();
		++m_changeGestureCount;
    }

    void Parameter::popChangeGesture()
    {
		assert(m_changeGestureCount > 0);
		--m_changeGestureCount;
		if(!m_changeGestureCount)
			endChangeGesture();
    }

    bool Parameter::isMetaParameter() const
    {
	    return !m_derivedParameters.empty();
    }

    void Parameter::setValue(const float _newValue)
	{
		// some plugin formats (VST3 for example) bounce back immediately, skip this, we don't
		// want it and VST2 doesn't do it either so why does Juce for VST3?
		// It's not the host, it's the Juce VST3 implementation
		if(m_notifyingHost)
			return;

		setUnnormalizedValue(juce::roundToInt(convertFrom0to1(_newValue)), Origin::HostAutomation);
	}

    void Parameter::setUnnormalizedValue(const int _newValue, const Origin _origin)
    {
		if (m_changingDerivedValues)
			return;

		m_lastValueOrigin = _origin;
		m_value.setValue(clampValue(_newValue));

		if(_origin != Origin::Derived)
			sendToSynth();

		forwardToDerived(_newValue);
    }

    void Parameter::setValueFromSynth(const int _newValue, const Origin _origin)
	{
		const auto clampedValue = clampValue(_newValue);

		// we do not want to send an excessive amount of value changes to the host if a preset is
		// changed, we use updateHostDisplay() (see caller) to inform the host to read all
		// parameters again instead
		const auto notifyHost = _origin != Origin::PresetChange;

		if (clampedValue != m_lastValue)
		{
			m_lastValue = clampedValue;
			m_lastValueOrigin = _origin;

			if (notifyHost && getDescription().isPublic)
			{
				setUnnormalizedValueNotifyingHost(clampedValue, _origin);
			}
			else
			{
				m_value.setValue(clampedValue);
			}
		}

		forwardToDerived(_newValue);
	}

    void Parameter::forwardToDerived(const int _newValue)
    {
		if (m_changingDerivedValues)
			return;

		m_changingDerivedValues = true;

		for (const auto& p : m_derivedParameters)
			p->setDerivedValue(_newValue);

		m_changingDerivedValues = false;
    }

    void Parameter::notifyHost(const float _value)
    {
		m_notifyingHost = true;
		sendValueChangedMessageToListeners(_value);
		m_notifyingHost = false;
    }

    juce::String Parameter::genId(const Description& d, const int part, const int uniqueId)
	{
		if(uniqueId > 0)
			return juce::String::formatted("%d_%d_%d_%d", static_cast<int>(d.page), part, d.index, uniqueId);
		return juce::String::formatted("%d_%d_%d", static_cast<int>(d.page), part, d.index);
	}

	float Parameter::getValueForText(const juce::String& _text) const
	{
		auto res = m_desc.valueList.textToValue(std::string(_text.getCharPointer()));
		if(m_desc.range.getStart() < 0)
			res += m_desc.range.getStart();
		return convertTo0to1(static_cast<float>(res));
	}

	ParamValue Parameter::getDefault() const
	{
		if(m_desc.defaultValue != Description::NoDefaultValue)
			return m_desc.defaultValue;
		return 0;
	}

	juce::String Parameter::getText(const float _normalisedValue, int _i) const
	{
		const auto v = convertFrom0to1(_normalisedValue);
		return m_desc.valueList.valueToText(juce::roundToInt(v) - std::min(0, m_desc.range.getStart()));
	}

	void Parameter::setLocked(const bool _locked)
	{
		if(m_isLocked == _locked)
			return;

		m_isLocked = _locked;

		onLockedChanged(this, m_isLocked);
	}

	void Parameter::addDerivedParameter(Parameter* _param)
	{
		if (_param == this)
			return;

		for (auto* p : m_derivedParameters)
		{
			_param->m_derivedParameters.insert(p);
			p->m_derivedParameters.insert(_param);
		}

		m_derivedParameters.insert(_param);
		_param->m_derivedParameters.insert(this);
	}

	Parameter::ScopedChangeGesture::ScopedChangeGesture(Parameter& _p) : m_parameter(_p)
    {
		if(_p.getDescription().isPublic)
		    _p.pushChangeGesture();
    }

    Parameter::ScopedChangeGesture::~ScopedChangeGesture()
    {
		if(m_parameter.getDescription().isPublic)
		    m_parameter.popChangeGesture();
    }

}
