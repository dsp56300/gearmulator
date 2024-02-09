#include "parameter.h"

#include "controller.h"

namespace pluginLib
{
	Parameter::Parameter(Controller &ctrl, const pluginLib::Description& _desc, const uint8_t _partNum, const int uniqueId) :
		juce::RangedAudioParameter(genId(_desc, _partNum, uniqueId), "Ch " + juce::String(_partNum + 1) + " " + _desc.displayName), m_ctrl(ctrl),
		m_desc(_desc), m_partNum(_partNum), m_uniqueId(uniqueId)
	{
		m_range.start = static_cast<float>(m_desc.range.getStart());
		m_range.end = static_cast<float>(m_desc.range.getEnd());
		m_range.interval = m_desc.step ? m_desc.step : (m_desc.isDiscrete || m_desc.isBool ? 1.0f : 0.0f);
		m_value.addListener(this);
    }

    void Parameter::valueChanged(juce::Value &)
    {
		sendToSynth();

		for (const auto& func : onValueChanged)
			func.second();
	}

    void Parameter::setDerivedValue(const int _value, ChangedBy _origin)
    {
		const int newValue = juce::roundToInt(m_range.getRange().clipValue(static_cast<float>(_value)));

		if (newValue == m_lastValue)
			return;

		m_lastValue = newValue;
		m_lastValueOrigin = ChangedBy::Derived;

		if(getDescription().isPublic)
		{
			const float v = convertTo0to1(static_cast<float>(newValue));

			switch (_origin)
			{
			case ChangedBy::ControlChange:
			case ChangedBy::HostAutomation:
			case ChangedBy::Derived:
				setValue(v, ChangedBy::Derived); 
				break;
			default:
				beginChangeGesture();
				setValueNotifyingHost(v, ChangedBy::Derived);
				endChangeGesture();
				break;
			}
		}
		else
		{
			m_value.setValue(newValue);
		}
	}

    void Parameter::sendToSynth()
    {
		const float floatValue = m_value.getValue();
		const auto value = juce::roundToInt(floatValue);

		jassert(m_range.getRange().contains(floatValue) || m_range.end == floatValue);
		jassert(value >= 0 && value <= 127);

		if (value == m_lastValue)
			return;

		// ignore initial update
		if (m_lastValue != -1)
			m_ctrl.sendParameterChange(*this, static_cast<uint8_t>(value));

		m_lastValue = value;
    }

    void Parameter::setValueNotifyingHost(const float _value, const ChangedBy _origin)
    {
		setValue(_value, _origin);
		sendValueChangedMessageToListeners(_value);
	}

    bool Parameter::isMetaParameter() const
    {
	    return !m_derivedParameters.empty();
    }

    void Parameter::setValue(const float _newValue)
	{
		setValue(_newValue, ChangedBy::HostAutomation);
	}

    void Parameter::setValue(const float _newValue, const ChangedBy _origin)
    {
		if (m_changingDerivedValues)
			return;

		const auto floatValue = convertFrom0to1(_newValue);
		m_lastValueOrigin = _origin;
		m_value.setValue(floatValue);

		sendToSynth();

		m_changingDerivedValues = true;

		for (const auto& parameter : m_derivedParameters)
		{
			if(!parameter->m_changingDerivedValues)
				parameter->setDerivedValue(m_value.getValue(), _origin);
		}

		m_changingDerivedValues = false;
    }

    void Parameter::setValueFromSynth(int newValue, const bool notifyHost, ChangedBy _origin)
	{
		const auto clampedValue = juce::roundToInt(m_range.getRange().clipValue(static_cast<float>(newValue)));

		if (clampedValue == m_lastValue)
			return;

		m_lastValue = clampedValue;
		m_lastValueOrigin = _origin;

		if (notifyHost && getDescription().isPublic)
		{
			beginChangeGesture();
			const auto v = convertTo0to1(static_cast<float>(clampedValue));
			setValueNotifyingHost(v, _origin);
			endChangeGesture();
		}
		else
		{
			m_value.setValue(clampedValue);
		}

		if (m_changingDerivedValues)
			return;

		m_changingDerivedValues = true;

		for (const auto& p : m_derivedParameters)
			p->setDerivedValue(newValue, _origin);

		m_changingDerivedValues = false;
	}

	bool Parameter::removeListener(const uint32_t _id)
	{
		bool res = false;
		for(auto it = onValueChanged.begin(); it !=onValueChanged.end();)
		{
			if(it->first == _id)
			{
				it = onValueChanged.erase(it);
				res = true;
			}
			else
			{
				++it;
			}
		}
		return res;
	}

	juce::String Parameter::genId(const pluginLib::Description &d, const int part, const int uniqueId)
	{
		if(uniqueId > 0)
			return juce::String::formatted("%d_%d_%d_%d", static_cast<int>(d.page), part, d.index, uniqueId);
		return juce::String::formatted("%d_%d_%d", static_cast<int>(d.page), part, d.index);
	}

	uint8_t Parameter::getDefault() const
	{
		if(m_desc.defaultValue  != Description::NoDefaultValue)
			return static_cast<uint8_t>(m_desc.defaultValue);
		return 0;
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
}
