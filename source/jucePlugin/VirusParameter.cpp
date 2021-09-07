#include "VirusParameter.h"

#include "VirusController.h"

namespace Virus
{
	Parameter::Parameter(Controller &ctrl, const Description desc, const uint8_t partNum) :
		m_ctrl(ctrl), m_desc(desc),
		m_partNum(partNum), juce::RangedAudioParameter(genId(desc, partNum),
													   "Ch " + juce::String(partNum + 1) + " " + desc.name)
	{
		m_range.start = m_desc.range.getStart();
		m_range.end = m_desc.range.getEnd();
		m_range.interval = m_desc.isDiscrete || m_desc.isBool ? 1 : 0;
		m_value.addListener(this);
    }

    void Parameter::valueChanged(juce::Value &)
    {
        const uint8_t value = static_cast<int>(m_value.getValue());
        jassert (m_range.getRange().contains(value) || m_range.end == value);
		if (value != m_lastValue)
		{
			m_ctrl.sendSysEx(
				m_ctrl.constructMessage({static_cast<uint8_t>(m_desc.page), m_partNum, m_desc.index, value}));
			m_lastValue = value;
		}
		if (onValueChanged)
			onValueChanged();
	}

	void Parameter::setValueFromSynth(int newValue, const bool notifyHost)
	{
		m_lastValue = newValue;
		if (notifyHost)
			setValueNotifyingHost(convertTo0to1(newValue));
		else
			m_value.setValue(newValue);
	}

	juce::String Parameter::genId(const Description &d, const int part)
	{
		return juce::String::formatted("%d_%d_%d", (int)d.page, part, d.index);
	}

} // namespace Virus
