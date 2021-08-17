#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../synthLib/plugin.h"
#include "VirusParameter.h"

class AudioPluginAudioProcessor;

namespace Virus
{
    using SysEx = std::vector<uint8_t>;
    class Controller : private juce::Timer
    {
    public:
        friend Parameter;
        static constexpr auto kNameLength = 10;

        Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);

        // this is called by the plug-in on audio thread!
        void dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &);

        void printMessage(const SysEx &) const;

        // currently Value as I figure out what's the best approach
        // ch - [0-15]
        // bank - [0-2] (ABC)
        // paramIndex - [0-127]
        juce::Value *getParam(uint8_t ch, uint8_t bank, uint8_t paramIndex);

        // bank - 0-1 (AB)
        juce::StringArray getSinglePresetNames(int bank) const;
        juce::StringArray getMultiPresetsName() const;
		bool isMultiMode() { return getParam(0, 2, 0x7a)->getValue(); }

	private:
		void timerCallback() override;

        static constexpr size_t kDataSizeInBytes = 256; // same for multi and single

        struct MultiPatch
        {
            uint8_t bankNumber;
            uint8_t progNumber;
            uint8_t data[kDataSizeInBytes];
        };

        struct SinglePatch
        {
            uint8_t bankNumber;
            uint8_t progNumber;
            uint8_t data[kDataSizeInBytes];
        };

        MultiPatch m_multis[128]; // RAM has 128 Multi 'snapshots'
        SinglePatch m_singles[2][128];

        static const std::initializer_list<Parameter::Description> m_paramsDescription;

        struct ParamIndex
        {
            uint8_t page;
            uint8_t partNum;
            uint8_t paramNum;
            bool operator<(ParamIndex const &rhs) const
            {
				if (page < rhs.page)         return false;
				if (page > rhs.page)         return true;
				if (partNum < rhs.partNum)   return false;
				if (partNum > rhs.partNum)   return true;
				if (paramNum < rhs.paramNum) return false;
				if (paramNum > rhs.paramNum) return true;
				return false;
			}
        };

        std::map<ParamIndex, std::unique_ptr<Parameter>> m_synthParams;

        void registerParams();

        // unchecked copy for patch data bytes
        static inline uint8_t copyData(const SysEx &src, int startPos, uint8_t *dst);

        template <typename T> juce::String parseAsciiText(const T &, int startPos) const;
        void parseMessage(const SysEx &);
        void parseSingle(const SysEx &);
        void parseMulti(const SysEx &);
        void parseParamChange(const SysEx &);
        void sendSysEx(const SysEx &);
        std::vector<uint8_t> constructMessage(SysEx msg);

        AudioPluginAudioProcessor &m_processor;
        juce::CriticalSection m_eventQueueLock;
        std::vector<synthLib::SMidiEvent> m_virusOut;
        unsigned char m_deviceId;
    };
}; // namespace Virus
