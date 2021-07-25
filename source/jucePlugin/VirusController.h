#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../synthLib/plugin.h"
class AudioPluginAudioProcessor;

namespace Virus
{
    using SysEx = std::vector<uint8_t>;
    class Controller
    {
    public:
        Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);

        // this is called by the plug-in on audio thread!
        void dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &);

        void printMessage(const SysEx &) const;

    private:
        void parseMessage(const SysEx &);
        void parseSingle(const SysEx &);
        void parseMulti(const SysEx &);
        void parseData(const SysEx &, size_t startPos);
        void sendSysEx(const SysEx &);
        std::vector<uint8_t> constructMessage(SysEx msg);

        AudioPluginAudioProcessor &m_processor;
        std::vector<synthLib::SMidiEvent> m_virusOut;
        unsigned char m_deviceId;
    };
}; // namespace Virus
