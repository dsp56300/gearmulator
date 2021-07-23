#include "VirusController.h"
#include "PluginProcessor.h"

namespace Virus
{
    Controller::Controller(AudioPluginAudioProcessor &p, unsigned char deviceId) : m_processor(p), m_deviceId(deviceId)
    {
    }

    std::vector<uint8_t> Controller::constructMessage(SysEx msg)
    {
        uint8_t start[] = {0xf0, 0x00, 0x20, 0x33, 0x01, static_cast<uint8_t>(m_deviceId)};
        msg.insert(msg.begin(), std::begin(start), std::end(start));
        msg.push_back(0xf7);
        return msg;
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
