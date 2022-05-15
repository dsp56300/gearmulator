#pragma once

#include "VirusParameter.h"
#include "VirusParameterType.h"

#include "../jucePluginLib/parameterdescriptions.h"
#include "../jucePluginLib/controller.h"

#include "../virusLib/microcontrollerTypes.h"

#include "../synthLib/plugin.h"

class AudioPluginAudioProcessor;

namespace Virus
{
    using SysEx = std::vector<uint8_t>;
    class Controller : public pluginLib::Controller, juce::Timer
    {
    public:
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

    	using Singles = std::array<std::array<SinglePatch, 128>, 8>;
        using Multis = std::array<MultiPatch, 128>;

    	static constexpr auto kNameLength = 10;

        Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
		~Controller() override;

        // this is called by the plug-in on audio thread!
        void dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &);

		void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;

    	pluginLib::Parameter* createParameter(pluginLib::Controller& _controller, const pluginLib::Description& _desc, uint8_t _part, int _uid) override;

        static void printMessage(const SysEx &);

        juce::Value* getParamValue(uint8_t ch, uint8_t bank, uint8_t paramIndex);

		virusLib::VirusModel getVirusModel() const;
        // bank - 0-1 (AB)
        juce::StringArray getSinglePresetNames(virusLib::BankNumber bank) const;

    	const Singles& getSinglePresets() const
        {
	        return m_singles;
        }

        juce::StringArray getMultiPresetsName() const;
		void setSinglePresetName(uint8_t part, juce::String _name);
		bool isMultiMode()
		{
            auto* value = getParamValue(0, 2, 0x7a);
            jassert(value);
			return value->getValue();
		}
		// part 0 - 15 (ignored when single! 0x40...)
		void setCurrentPartPreset(uint8_t _part, virusLib::BankNumber _bank, uint8_t _prg);
        virusLib::BankNumber getCurrentPartBank(uint8_t part) const;
		uint8_t getCurrentPartProgram(uint8_t part) const;
		juce::String getCurrentPartPresetName(uint8_t part);
		uint32_t getBankCount() const { return static_cast<uint32_t>(m_singles.size()); }
		uint8_t getCurrentPart() const { return m_currentPart; }
		void setCurrentPart(uint8_t _part) { m_currentPart = _part; }
		void parseMessage(const SysEx &);
		void sendSysEx(const SysEx &) const;
        void onStateLoaded() const;
		juce::PropertiesFile* getConfig() { return m_config; }
		std::function<void()> onProgramChange = {};
		std::function<void()> onMsgDone = {};
		std::vector<uint8_t> constructMessage(SysEx msg) const;

        uint8_t getDeviceId() const { return m_deviceId; }

    private:
		void timerCallback() override;

        Multis m_multis; // RAM has 128 Multi 'snapshots'
        Singles m_singles;
		MultiPatch m_currentMulti;

		// unchecked copy for patch data bytes
        static inline uint8_t copyData(const SysEx &src, int startPos, std::array<uint8_t, kDataSizeInBytes>& dst);

        template <typename T> static juce::String parseAsciiText(const T &, int startPos);
        void parseSingle(const SysEx &);
        void parseMulti(const SysEx &);
        void parseParamChange(const SysEx &);
        void parseControllerDump(synthLib::SMidiEvent &);

        AudioPluginAudioProcessor& m_processor;
        juce::CriticalSection m_eventQueueLock;
        std::vector<synthLib::SMidiEvent> m_virusOut;
        unsigned char m_deviceId;
        virusLib::BankNumber m_currentBank[16]{};
        uint8_t m_currentProgram[16]{};
		uint8_t m_currentPart = 0;
		juce::PropertiesFile *m_config;
    };
}; // namespace Virus
