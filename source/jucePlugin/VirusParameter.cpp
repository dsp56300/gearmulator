#include "VirusParameter.h"

#include "VirusController.h"

namespace Virus
{
    Parameter::Parameter(Controller &ctrl, const Description desc, const uint8_t partNum) :
        m_ctrl(ctrl), m_desc(desc), m_partNum(partNum)
    {
        m_value.addListener(this);
    }

    void Parameter::valueChanged(juce::Value &)
    {
        const uint8_t value = static_cast<int>(m_value.getValue());
        m_ctrl.sendSysEx(m_ctrl.constructMessage({static_cast<uint8_t>(m_desc.page), m_partNum, m_desc.index, value}));
    }
} // namespace Virus
