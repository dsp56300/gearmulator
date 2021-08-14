#include "VirusParameter.h"

#include "VirusController.h"

namespace Virus
{
	Parameter::Parameter(Controller &ctrl, const Description desc, const uint8_t partNum) :
		m_ctrl(ctrl), m_desc(desc), m_partNum(partNum), juce::RangedAudioParameter(genId(), (m_desc.classFlags | Class::GLOBAL) ? "" : ("Ch " + juce::String(m_partNum + 1) + " ") + m_desc.name)
	{
		m_range.start = m_desc.range.getStart();
		m_range.end = m_desc.range.getEnd();
		m_value.addListener(this);
    }

    void Parameter::valueChanged(juce::Value &)
    {
        const uint8_t value = static_cast<int>(m_value.getValue());
        m_ctrl.sendSysEx(m_ctrl.constructMessage({static_cast<uint8_t>(m_desc.page), m_partNum, m_desc.index, value}));
    }

    juce::String Parameter::genId()
    {
        return juce::String::formatted("%d_%d_%d", (int)m_desc.page, m_partNum, m_paramNum);
    }

} // namespace Virus
