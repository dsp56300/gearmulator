#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../synthLib/plugin.h"
#include "VirusParameter.h"
#include "VirusParameterDescription.h"

namespace virusLib
{
	enum class BankNumber : uint8_t;
}

class AudioPluginAudioProcessor;

namespace Virus
{
	enum EnvelopeType
	{
		Env_Amp,
		Env_Filter
	};
    using SysEx = std::vector<uint8_t>;
    class Controller : private juce::Timer
    {
    public:
        friend Parameter;
        static constexpr auto kNameLength = 10;

        Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
		~Controller();
        // this is called by the plug-in on audio thread!
        void dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &);

        void printMessage(const SysEx &) const;

        // currently Value as I figure out what's the best approach
        // ch - [0-15]
        // bank - [0-2] (ABC)
        // paramIndex - [0-127]
        juce::Value *getParamValue(uint8_t ch, uint8_t bank, uint8_t paramIndex);
        juce::Value *getParamValue(ParameterType _param);
        Parameter* getParameter(ParameterType _param);
        Parameter *getParameter(ParameterType _param, uint8_t _part);
		uint8_t getVirusModel();
        // bank - 0-1 (AB)
        juce::StringArray getSinglePresetNames(virusLib::BankNumber bank) const;
        juce::StringArray getMultiPresetsName() const;
		void setSinglePresetName(uint8_t part, juce::String _name);
		bool isMultiMode() { return getParamValue(0, 2, 0x7a)->getValue(); }
		// part 0 - 15 (ignored when single! 0x40...)
		void setCurrentPartPreset(uint8_t part, virusLib::BankNumber bank, uint8_t prg);
        virusLib::BankNumber getCurrentPartBank(uint8_t part);
		uint8_t getCurrentPartProgram(uint8_t part) const;
		juce::String getCurrentPartPresetName(uint8_t part);
		uint32_t getBankCount() const { return static_cast<uint32_t>(m_singles.size()); }
		uint8_t getCurrentPart() const { return m_currentPart; }
		void setCurrentPart(uint8_t _part) { m_currentPart = _part; }
		void parseMessage(const SysEx &);
		void sendSysEx(const SysEx &);
        void onStateLoaded();
		juce::PropertiesFile* getConfig() { return m_config; }
		std::function<void()> onProgramChange = {};
		std::function<void()> onMsgDone = {};

    private:
		void timerCallback() override;
        static constexpr size_t kDataSizeInBytes = 256; // same for multi and single

        struct MultiPatch
        {
            uint8_t bankNumber = 0;
            uint8_t progNumber = 0;
			std::array<uint8_t, kDataSizeInBytes> data{};
        };

        struct SinglePatch
        {
	        virusLib::BankNumber bankNumber = static_cast<virusLib::BankNumber>(0);
            uint8_t progNumber = 0;
			std::array<uint8_t, kDataSizeInBytes> data{};
        };

        MultiPatch m_multis[128]; // RAM has 128 Multi 'snapshots'
        std::array<std::array<SinglePatch, 128>, 8> m_singles;
		MultiPatch m_currentMulti;

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

		std::map<ParamIndex, std::unique_ptr<Parameter>> m_synthInternalParams;
		std::map<ParamIndex, Parameter *> m_synthParams; // exposed and managed by audio processor
		std::map<ParameterType, ParamIndex> m_paramTypeToParamIndex;

		void registerParams();
		// tries to find synth param in both internal and host.
		// @return found parameter or nullptr if none found.
		Parameter *findSynthParam(uint8_t _part, uint8_t _page, uint8_t _paramIndex);
		Parameter* findSynthParam(const ParamIndex& _paramIndex);

		// unchecked copy for patch data bytes
        static inline uint8_t copyData(const SysEx &src, int startPos, std::array<uint8_t, kDataSizeInBytes>& dst);

        template <typename T> juce::String parseAsciiText(const T &, int startPos) const;
        void parseSingle(const SysEx &);
        void parseMulti(const SysEx &);
        void parseParamChange(const SysEx &);
        void parseControllerDump(synthLib::SMidiEvent &);

        std::vector<uint8_t> constructMessage(SysEx msg);

        AudioPluginAudioProcessor &m_processor;
        juce::CriticalSection m_eventQueueLock;
        std::vector<synthLib::SMidiEvent> m_virusOut;
        unsigned char m_deviceId;
        virusLib::BankNumber m_currentBank[16];
        uint8_t m_currentProgram[16];
		uint8_t m_currentPart = 0;
		juce::PropertiesFile *m_config;
    };
}; // namespace Virus
