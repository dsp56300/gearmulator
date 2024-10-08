#pragma once

#include "jucePluginLib/parameterdescriptions.h"
#include "jucePluginLib/controller.h"
#include "jucePluginLib/event.h"

#include "virusLib/frontpanelState.h"
#include "virusLib/microcontrollerTypes.h"
#include "virusLib/romfile.h"

namespace virus
{
	class VirusProcessor;

    class Controller : public pluginLib::Controller
    {
    public:
		pluginLib::Event<virusLib::FrontpanelState> onFrontPanelStateChanged;

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
            SingleDump_C,

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

    	Controller(VirusProcessor&, virusLib::DeviceModel _defaultModel, unsigned char deviceId = 0x00);
		~Controller() override;

        std::vector<uint8_t> createSingleDump(uint8_t _part, uint8_t _bank, uint8_t _program);
        std::vector<uint8_t> createSingleDump(MidiPacketType _packet, uint8_t _bank, uint8_t _program, const pluginLib::MidiPacket::AnyPartParamValues& _paramValues);
        std::vector<uint8_t> modifySingleDump(const std::vector<uint8_t>& _sysex, virusLib::BankNumber _newBank, uint8_t _newProgram) const;

    	void selectPrevPreset(uint8_t _part);
    	void selectNextPreset(uint8_t _part);
        std::string getBankName(uint32_t _index) const;

    	bool activatePatch(const std::vector<unsigned char>& _sysex);
    	bool activatePatch(const std::vector<unsigned char>& _sysex, uint32_t _part);

        static void printMessage(const pluginLib::SysEx &);

        juce::StringArray getSinglePresetNames(virusLib::BankNumber bank) const;
        std::string getSinglePresetName(const pluginLib::MidiPacket::ParamValues& _values) const;
        std::string getSinglePresetName(const pluginLib::MidiPacket::AnyPartParamValues& _values) const;
        std::string getMultiPresetName(const pluginLib::MidiPacket::ParamValues& _values) const;
        std::string getPresetName(const std::string& _paramNamePrefix, const pluginLib::MidiPacket::ParamValues& _values) const;
        std::string getPresetName(const std::string& _paramNamePrefix, const pluginLib::MidiPacket::AnyPartParamValues& _values) const;

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

		void setSinglePresetName(uint8_t _part, const juce::String& _name) const;
        void setSinglePresetName(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _name) const;

    	bool isMultiMode() const;

    	// part 0 - 15 (ignored when single! 0x40...)
		void setCurrentPartPreset(uint8_t _part, virusLib::BankNumber _bank, uint8_t _prg);
        void setCurrentPartPresetSource(uint8_t _part, PresetSource _source);

    	virusLib::BankNumber getCurrentPartBank(uint8_t _part) const;
		uint8_t getCurrentPartProgram(uint8_t _part) const;
		PresetSource getCurrentPartPresetSource(uint8_t _part) const;

		juce::String getCurrentPartPresetName(uint8_t _part) const;
		uint32_t getBankCount() const { return static_cast<uint32_t>(m_singles.size()); }
		bool parseSysexMessage(const pluginLib::SysEx& _msg, synthLib::MidiEventSource _source) override;
		bool parseControllerMessage(const synthLib::SMidiEvent&) override;
        void onStateLoaded() override;

		uint8_t getPartCount() const override;

		std::function<void(int)> onProgramChange = {};
		std::function<void()> onMsgDone = {};
		std::function<void(virusLib::BankNumber _bank, uint32_t _program)> onRomPatchReceived = {};

		bool requestProgram(uint8_t _bank, uint8_t _program, bool _multi) const;
		bool requestSingle(uint8_t _bank, uint8_t _program) const;
		bool requestMulti(uint8_t _bank, uint8_t _program) const;

    	bool requestSingleBank(uint8_t _bank) const;

    	bool requestTotal() const;
    	bool requestArrangement() const;
		void requestRomBanks();
        
		void sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value) override;
		bool sendParameterChange(uint8_t _page, uint8_t _part, uint8_t _index, uint8_t _value) const;

        using pluginLib::Controller::sendSysEx;

        bool sendSysEx(MidiPacketType _type) const;
        bool sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

		uint8_t getDeviceId() const { return m_deviceId; }

        bool parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _parameterValues, const pluginLib::SysEx& _msg) const;
        bool parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _parameterValues, const pluginLib::SysEx& _msg, MidiPacketType& usedPacketType) const;

		const auto& getFrontpanelState() const { return m_frontpanelState; }

    private:
        Singles m_singles;
        SinglePatch m_singleEditBuffer;                     // single mode
        std::array<SinglePatch, 16> m_singleEditBuffers;    // multi mode

        MultiPatch m_multiEditBuffer;

        void parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues);

    	void parseMulti(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues);

        void parseParamChange(const pluginLib::MidiPacket::Data& _data);
        bool parseControllerDump(const synthLib::SMidiEvent&);

        VirusProcessor& m_processor;
        virusLib::DeviceModel m_defaultModel;

        unsigned char m_deviceId;
        virusLib::BankNumber m_currentBank[16]{};
        uint8_t m_currentProgram[16]{};
        PresetSource m_currentPresetSource[16]{PresetSource::Unknown};
		pluginLib::EventListener<const virusLib::ROMFile*> m_onRomChanged;
        virusLib::FrontpanelState m_frontpanelState;
    };
}; // namespace Virus
