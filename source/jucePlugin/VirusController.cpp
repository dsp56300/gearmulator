#include "VirusController.h"
#include "PluginProcessor.h"

namespace Virus
{
    Controller::Controller(AudioPluginAudioProcessor &p, unsigned char deviceId) : m_processor(p), m_deviceId(deviceId)
    {
    }

    void Controller::dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &newData)
    {
        m_virusOut = newData;
        for (auto msg : m_virusOut)
        {
            // parse here
        }
    }
}; // namespace Virus
