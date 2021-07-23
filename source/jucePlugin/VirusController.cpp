#include "VirusController.h"
#include "PluginProcessor.h"

// TODO: all sysex structs can be refactored to common instead of including this!
#include "../virusLib/microcontroller.h"

using MessageType = virusLib::Microcontroller::SysexMessageType;

namespace Virus
{
    static constexpr uint8_t kSysExStart[] = {0xf0, 0x00, 0x20, 0x33, 0x01};

    Controller::Controller(AudioPluginAudioProcessor &p, unsigned char deviceId) : m_processor(p), m_deviceId(deviceId)
    {
        sendSysEx(constructMessage({0x30, 0x00, 0x00}));
    }

    void Controller::parseMessage(const SysEx &msg)
    {
        if (msg.size() < 8)
            return; // shorter than expected!

        if (msg[msg.size() - 1] != 0xf7)
            return; // invalid end?!?

        for (auto i = 0; i < msg.size(); ++i)
        {
            if (i < 5)
            {
                if (msg[i] != kSysExStart[i])
                    return; // invalid header
            }
            else if (i == 5)
            {
                if (msg[i] != m_deviceId)
                    return; // not intended to this device!
            }
            else if (i == 6)
            {
                switch (msg[i])
                {
                case MessageType::DUMP_SINGLE:
                    std::cout << "Parse Single" << std::endl;
                    break;
                case MessageType::DUMP_MULTI:
                    std::cout << "Parse Multi" << std::endl;
                    break;
                }
            }
        }
    }

    void Controller::printMessage(const SysEx &msg) const
    {
        for (auto &m : msg)
        {
            std::cout << std::hex << (int)m << ",";
        }
        std::cout << std::endl;
    }

    void Controller::sendSysEx(const SysEx &msg)
    {
        synthLib::SMidiEvent ev;
        ev.sysex = msg;
        m_processor.addMidiEvent(ev);
    }

    std::vector<uint8_t> Controller::constructMessage(SysEx msg)
    {
        const uint8_t start[] = {0xf0, 0x00, 0x20, 0x33, 0x01, static_cast<uint8_t>(m_deviceId)};
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
            parseMessage(msg.sysex);
        }
    }
}; // namespace Virus
