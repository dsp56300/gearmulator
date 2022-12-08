#pragma once

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
        struct Patch
        {
            std::string name;
			std::vector<uint8_t> data;
            uint8_t progNumber = 0;
        };

        struct SinglePatch : Patch
        {
	        virusLib::BankNumber bankNumber = static_cast<virusLib::BankNumber>(0);
        };

        struct MultiPatch : Patch {};

    	using Singles = std::vector<std::array<SinglePatch, 128>>;

    	static constexpr auto kNameLength = 10;

		enum class MidiPacketType
		{
			RequestSingle,
			RequestMulti,
			RequestSingleBank,
			RequestMultiBank,
			RequestArrangement,
			RequestGlobal,
			RequestTotal,
			RequestControllerDump,
			ParameterChange,
			SingleDump,
			MultiDump,

			Count
		};

        enum class PresetSource
        {
            Unknown,
	        Rom,
            Browser
        };

        struct CurrentPreset
        {
	        uint8_t program = 0;
	        virusLib::BankNumber bank = virusLib::BankNumber::EditBuffer;
            PresetSource source = PresetSource::Unknown;
        };

    	Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
		~Controller() override;

        // this is called by the plug-in on audio thread!
        void dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &);

        std::vector<uint8_t> createSingleDump(uint8_t _part, uint8_t _bank, uint8_t _program);
        std::vector<uint8_t> createSingleDump(uint8_t _bank, uint8_t _program, const pluginLib::MidiPacket::ParamValues& _paramValues);
        std::vector<uint8_t> modifySingleDump(const std::vector<uint8_t>& _sysex, virusLib::BankNumber _newBank, uint8_t _newProgram, bool _modifyBank, bool _modifyProgram);

    	void selectPrevPreset(uint8_t _part);
    	void selectNextPreset(uint8_t _part);

        static void printMessage(const SysEx &);

        juce::Value* getParamValue(uint8_t ch, uint8_t bank, uint8_t paramIndex);

        juce::StringArray getSinglePresetNames(virusLib::BankNumber bank) const;
        std::string getSinglePresetName(const pluginLib::MidiPacket::ParamValues& _values) const;
        std::string getMultiPresetName(const pluginLib::MidiPacket::ParamValues& _values) const;
        std::string getPresetName(const std::string& _paramNamePrefix, const pluginLib::MidiPacket::ParamValues& _values) const;

    	const Singles& getSinglePresets() const
        {
	        return m_singles;
        }

        const SinglePatch& getSingleEditBuffer() const
    	{
    		return m_singleEditBuffer;
    	}

        const SinglePatch& getSingleEditBuffer(const uint8_t _part) const
    	{
    		return m_singleEditBuffers[_part];
    	}

        const MultiPatch& getMultiEditBuffer() const
    	{
    		return m_multiEditBuffer;
    	}

		void setSinglePresetName(uint8_t _part, const juce::String& _name);
		bool isMultiMode() const;

    	// part 0 - 15 (ignored when single! 0x40...)
		void setCurrentPartPreset(uint8_t _part, virusLib::BankNumber _bank, uint8_t _prg);
        void setCurrentPartPresetSource(uint8_t _part, PresetSource _source);

    	virusLib::BankNumber getCurrentPartBank(uint8_t _part) const;
		uint8_t getCurrentPartProgram(uint8_t _part) const;
		PresetSource getCurrentPartPresetSource(uint8_t _part) const;

		juce::String getCurrentPartPresetName(uint8_t _part) const;
		uint32_t getBankCount() const { return static_cast<uint32_t>(m_singles.size()); }
		uint8_t getCurrentPart() const { return m_currentPart; }
		void setCurrentPart(uint8_t _part) { m_currentPart = _part; }
		void parseMessage(const SysEx &);
		void sendSysEx(const SysEx &) const;
        void onStateLoaded() const;
		juce::PropertiesFile* getConfig() { return m_config; }
		std::function<void()> onProgramChange = {};
		std::function<void()> onMsgDone = {};

		bool requestProgram(uint8_t _bank, uint8_t _program, bool _multi) const;
		bool requestSingle(uint8_t _bank, uint8_t _program) const;
		bool requestMulti(uint8_t _bank, uint8_t _program) const;

    	bool requestSingleBank(uint8_t _bank) const;

    	bool requestTotal() const;
    	bool requestArrangement() const;
        
		void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;
		bool sendParameterChange(uint8_t _page, uint8_t _part, uint8_t _index, uint8_t _value) const;

        bool sendSysEx(MidiPacketType _type) const;
        bool sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

		uint8_t getDeviceId() const { return m_deviceId; }

        bool parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::ParamValues& _parameterValues, const SysEx& _msg) const;

    private:
        static std::string loadParameterDescriptions();

		void timerCallback() override;

        Singles m_singles;
        SinglePatch m_singleEditBuffer;                     // single mode
        std::array<SinglePatch, 16> m_singleEditBuffers;    // multi mode

        MultiPatch m_multiEditBuffer;

        void parseSingle(const SysEx& _msg);
        void parseSingle(const SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues);

    	void parseMulti(const SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues);

        void parseParamChange(const pluginLib::MidiPacket::Data& _data);
        void parseControllerDump(synthLib::SMidiEvent &);

        AudioPluginAudioProcessor& m_processor;
        juce::CriticalSection m_eventQueueLock;
        std::vector<synthLib::SMidiEvent> m_virusOut;
        unsigned char m_deviceId;
        virusLib::BankNumber m_currentBank[16]{};
        uint8_t m_currentProgram[16]{};
        PresetSource m_currentPresetSource[16]{PresetSource::Unknown};
		uint8_t m_currentPart = 0;
		juce::PropertiesFile *m_config;
    };
}; // namespace Virus
