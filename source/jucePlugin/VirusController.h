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
        static constexpr auto kNameLength = 10;

        Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);

        // this is called by the plug-in on audio thread!
        void dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &);

        void printMessage(const SysEx &) const;

    private:
        static constexpr size_t kDataSizeInBytes = 256; // same for multi and single

        struct MultiPatch
        {
            uint8_t bankNumber;
            uint8_t progNumber;
            uint8_t data[kDataSizeInBytes];
        };

        MultiPatch m_multis[128]; // RAM has 128 Multi 'snapshots'

        // unchecked copy for patch data bytes
        static inline uint8_t copyData(const SysEx &src, int startPos, uint8_t *dst);

        juce::String parseAsciiText(const SysEx &, int startPos);
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
