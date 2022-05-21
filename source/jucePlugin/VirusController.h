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
        static constexpr size_t kDataSizeInBytes = 256; // same for multi and single

        struct SinglePatch
        {
	        virusLib::BankNumber bankNumber = static_cast<virusLib::BankNumber>(0);
            uint8_t progNumber = 0;
            std::string name;
			std::vector<uint8_t> data;
        };

    	using Singles = std::array<std::array<SinglePatch, 128>, 8>;

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

    	Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
		~Controller() override;

        // this is called by the plug-in on audio thread!
        void dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &);

    	pluginLib::Parameter* createParameter(pluginLib::Controller& _controller, const pluginLib::Description& _desc, uint8_t _part, int _uid) override;
        std::vector<uint8_t> createSingleDump(uint8_t _part, uint8_t _bank, uint8_t _program);
        std::vector<uint8_t> createSingleDump(uint8_t _bank, uint8_t _program, const pluginLib::MidiPacket::ParamValues& _paramValues);
        std::vector<uint8_t> modifySingleDump(const std::vector<uint8_t>& _sysex, virusLib::BankNumber _newBank, uint8_t _newProgram, bool _modifyBank, bool _modifyProgram);

        static void printMessage(const SysEx &);

        juce::Value* getParamValue(uint8_t ch, uint8_t bank, uint8_t paramIndex);

        juce::StringArray getSinglePresetNames(virusLib::BankNumber bank) const;
        std::string getSinglePresetName(const pluginLib::MidiPacket::ParamValues& _values) const;

    	const Singles& getSinglePresets() const
        {
	        return m_singles;
        }

		void setSinglePresetName(uint8_t _part, const juce::String& _name);
		bool isMultiMode() const;
        // part 0 - 15 (ignored when single! 0x40...)
		void setCurrentPartPreset(uint8_t _part, virusLib::BankNumber _bank, uint8_t _prg);
        virusLib::BankNumber getCurrentPartBank(uint8_t _part) const;
		uint8_t getCurrentPartProgram(uint8_t _part) const;
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

        void parseSingle(const SysEx& _msg);
        void parseSingle(const SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues);

        void parseMulti(const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues);
        void parseParamChange(const pluginLib::MidiPacket::Data& _data);
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
