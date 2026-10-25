#pragma once

#include <atomic>

#include "jucePluginLib/parameterdescriptions.h"
#include "jucePluginLib/controller.h"

#include "baseLib/event.h"

#include "virusLib/frontpanelState.h"
#include "virusLib/microcontrollerTypes.h"
#include "virusLib/romfile.h"

#include <atomic>

namespace virus
{
	class VirusProcessor;

    class Controller : public pluginLib::Controller, juce::AsyncUpdater
    {
    public:
		baseLib::Event<virusLib::FrontpanelState> onFrontPanelStateChanged;

	    struct Patch
	    {
	        std::string name;
			synthLib::SysexBuffer data;
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

        synthLib::SysexBuffer createSingleDump(uint8_t _part, uint8_t _bank, uint8_t _program);
        synthLib::SysexBuffer createSingleDump(MidiPacketType _packet, uint8_t _bank, uint8_t _program, const pluginLib::MidiPacket::AnyPartParamValues& _paramValues);
        synthLib::SysexBuffer modifySingleDump(const synthLib::SysexBuffer& _sysex, virusLib::BankNumber _newBank, uint8_t _newProgram) const;

    	void selectPrevPreset(uint8_t _part);
    	void selectNextPreset(uint8_t _part);
        std::string getBankName(uint32_t _index) const;

    	bool activatePatch(const synthLib::SysexBuffer& _sysex);
    	bool activatePatch(const synthLib::SysexBuffer& _sysex, uint32_t _part);

    	// The edit buffer a single for _part is loaded into: the single one for part 0 in single mode. False for a
    	// part that does not exist. Safe to call from any thread
    	static bool getSingleEditBufferProgram(uint32_t _part, bool _multiMode, uint8_t& _program);

    	// what activatePatch() does once a single is sent to edit buffer _program
    	void onSingleActivated(uint8_t _program);

        static void printMessage(const pluginLib::SysEx &);

        juce::StringArray getSinglePresetNames(virusLib::BankNumber bank) const;
        std::string getSinglePresetName(const pluginLib::MidiPacket::ParamValues& _values) const;
        std::string getSinglePresetName(const pluginLib::MidiPacket::AnyPartParamValues& _values) const;
        std::string getMultiPresetName(const pluginLib::MidiPacket::ParamValues& _values) const;

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

		std::string getMultiPresetName() const;
		void setMultiPresetName(const juce::String& _name) const;


    	bool isMultiMode() const;
    	bool isPartEnabled(const uint8_t _part) const override { return _part == 0 || isMultiMode(); }

    	// the same, for threads that must not read parameters, the audio thread for example
    	bool isMultiModeAnyThread() const { return m_multiMode; }

    	// switches between single and multi mode and requests the arrangement of the new mode
    	void setPlayMode(uint8_t _playMode);

    	// fired by setPlayMode()
    	baseLib::Event<uint8_t> evPlayModeSet;

    	// part 0 - 15 (ignored when single! 0x40...)
		void setCurrentPartPreset(uint8_t _part, virusLib::BankNumber _bank, uint8_t _prg);
        void setCurrentPartPresetSource(uint8_t _part, PresetSource _source);

    	virusLib::BankNumber getCurrentPartBank(uint8_t _part) const;
		uint8_t getCurrentPartProgram(uint8_t _part) const;
		PresetSource getCurrentPartPresetSource(uint8_t _part) const;

		std::string getCurrentPartPresetName(uint8_t _part) const;
		uint32_t getBankCount() const { return static_cast<uint32_t>(m_singles.size()); }
		bool parseSysexMessage(const pluginLib::SysEx& _msg, synthLib::MidiEventSource _source) override;
		bool parseControllerMessage(const synthLib::SMidiEvent&) override;
        void onStateLoaded() override;

		uint8_t getPartCount() const override;

		std::function<void(int)> onProgramChange = {};
		std::function<void()> onMsgDone = {};
		std::function<void(virusLib::BankNumber, uint32_t)> onRomPatchReceived = {};
		std::function<void()> onMultiReceived = {};

		bool requestProgram(uint8_t _bank, uint8_t _program, bool _multi) const;
		bool requestSingle(uint8_t _bank, uint8_t _program) const;
		bool requestMulti(uint8_t _bank, uint8_t _program) const;

    	bool requestSingleBank(uint8_t _bank) const;

    	bool requestTotal() const;
    	bool requestArrangement() const;
		void requestRomBanks();
        
		void sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value, pluginLib::Parameter::Origin _origin) override;
		bool sendParameterChange(uint8_t _page, uint8_t _part, uint8_t _index, uint8_t _value) const;

        using pluginLib::Controller::sendSysEx;

        bool sendSysEx(MidiPacketType _type) const;
        bool sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

		uint8_t getDeviceId() const { return m_deviceId; }

        bool parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _parameterValues, const pluginLib::SysEx& _msg) const;
        bool parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _parameterValues, const pluginLib::SysEx& _msg, MidiPacketType& usedPacketType) const;

		const auto& getFrontpanelState() const { return m_frontpanelState; }

		std::vector<uint8_t> getPartsForMidiChannel(uint8_t _channel) override;

		bool getLedBrightness(const std::string& _name, float& _brightness) const override;

		// audio thread: the host tempo in units of the Clock Tempo parameter, which follows it on the message thread
		void setHostClockTempo(int _value);

    private:
		void handleAsyncUpdate() override;

        Singles m_singles;
        SinglePatch m_singleEditBuffer;                     // single mode
        std::array<SinglePatch, 16> m_singleEditBuffers;    // multi mode

        MultiPatch m_multiEditBuffer;

        void parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues);

    	void parseMulti(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues);

        void parseParamChange(const pluginLib::MidiPacket::Data& _data, synthLib::MidiEventSource _source);
        bool parseControllerDump(const synthLib::SMidiEvent&);

        VirusProcessor& m_processor;
        virusLib::DeviceModel m_defaultModel;

        unsigned char m_deviceId;
        virusLib::BankNumber m_currentBank[16]{};
        uint8_t m_currentProgram[16]{};
        PresetSource m_currentPresetSource[16]{PresetSource::Unknown};
		baseLib::EventListener<uint32_t> m_onRomChanged;
        virusLib::FrontpanelState m_frontpanelState;
		// LFO 1-3 and the logo for skins, linear, negative for an LED this model does not drive. Written with the
		// front panel state, read from any thread
		std::array<std::atomic<float>, 4> m_ledBrightness{{{-1.0f}, {-1.0f}, {-1.0f}, {-1.0f}}};
        std::atomic<bool> m_multiMode = false;

		pluginLib::Parameter* m_clockTempo = nullptr;
		std::atomic<int> m_hostClockTempo{-1};			// -1 = the host did not tell a tempo yet
		std::atomic<int> m_clockTempoValue{-1};			// the value of m_clockTempo, for the audio thread
		baseLib::EventListener<pluginLib::Parameter*> m_onClockTempoChanged;
    };
}; // namespace Virus
