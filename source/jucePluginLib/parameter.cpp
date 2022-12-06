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
		m_range.interval = m_desc.isDiscrete || m_desc.isBool ? 1.0f : 0.0f;
		m_value.addListener(this);
    }

    void Parameter::valueChanged(juce::Value &)
    {
		const uint8_t value = roundToInt(m_value.getValue());
		jassert (m_range.getRange().contains(value) || m_range.end == value);
		if (value != m_lastValue)
		{
			// ignore initial update
			if(m_lastValue != -1)
				m_ctrl.sendParameterChange(*this, value);
			m_lastValue = value;
		}

		for (const auto& func : onValueChanged)
			func.second();
	}

    void Parameter::setDerivedValue(const int _value)
    {
		const int newValue = juce::roundToInt(m_range.getRange().clipValue(static_cast<float>(_value)));

		if (newValue == m_lastValue)
			return;

		m_lastValue = newValue;

		if(getDescription().isPublic)
		{
			beginChangeGesture();
			setValueNotifyingHost(convertTo0to1(static_cast<float>(newValue)));
			endChangeGesture();
		}
		else
		{
			m_value.setValue(newValue);
		}
	}

    bool Parameter::isMetaParameter() const
    {
	    return !m_derivedParameters.empty();
    }

    void Parameter::setValue(float newValue)
	{
		if (m_changingDerivedValues)
			return;

		m_value.setValue(convertFrom0to1(newValue));

		m_changingDerivedValues = true;

		for (const auto& parameter : m_derivedParameters)
		{
			if(!parameter->m_changingDerivedValues)
				parameter->setDerivedValue(m_value.getValue());
		}

		m_changingDerivedValues = false;
	}

	void Parameter::setValueFromSynth(int newValue, const bool notifyHost)
	{
		const auto clampedValue = juce::roundToInt(m_range.getRange().clipValue(static_cast<float>(newValue)));

		if (clampedValue == m_lastValue)
			return;

		m_lastValue = clampedValue;

		if (notifyHost && getDescription().isPublic)
		{
			beginChangeGesture();
			setValueNotifyingHost(convertTo0to1(static_cast<float>(clampedValue)));
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
			p->setDerivedValue(newValue);

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
