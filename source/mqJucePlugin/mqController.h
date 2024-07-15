#pragma once

#include "jucePluginLib/controller.h"

#include "mqLib/mqmiditypes.h"
#include "jucePluginLib/event.h"

namespace mqJucePlugin
{
	class FrontPanel;
}

class AudioPluginAudioProcessor;

class Controller : public pluginLib::Controller
{
public:
    enum MidiPacketType
    {
        RequestSingle,
        RequestMulti,
        RequestDrum,
        RequestSingleBank,
        RequestMultiBank,
        RequestDrumBank,
        RequestGlobal,
        RequestAllSingles,
        SingleParameterChange,
        MultiParameterChange,
        GlobalParameterChange,
        SingleDump,
        SingleDumpQ,
		MultiDump,
        GlobalDump,
        EmuRequestLcd,
        EmuRequestLeds,
        EmuSendButton,
        EmuSendRotary,

        Count
    };

    struct Patch
    {
        std::string name;
		std::vector<uint8_t> data;
    };

	pluginLib::Event<bool> onPlayModeChanged;

    Controller(AudioPluginAudioProcessor &, unsigned char _deviceId = 0);
	~Controller() override;

    void setFrontPanel(mqJucePlugin::FrontPanel* _frontPanel);
    void sendSingle(const std::vector<uint8_t>& _sysex);
    void sendSingle(const std::vector<uint8_t>& _sysex, uint8_t _part);

	bool sendSysEx(MidiPacketType _type) const;
    bool sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

    bool isMultiMode() const;
    void setPlayMode(bool _multiMode);

    void selectNextPreset();
    void selectPrevPreset();

	std::vector<uint8_t> createSingleDump(mqLib::MidiBufferNum _buffer, mqLib::MidiSoundLocation _location, uint8_t _locationOffset, uint8_t _part) const;
	std::vector<uint8_t> createSingleDump(mqLib::MidiBufferNum _buffer, mqLib::MidiSoundLocation _location, uint8_t _locationOffset, const pluginLib::MidiPacket
	                                      ::AnyPartParamValues& _values) const;
    bool parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _paramValues, const std::vector<uint8_t>& _sysex) const;

	std::string getSingleName(const pluginLib::MidiPacket::ParamValues& _values) const;
    std::string getSingleName(const pluginLib::MidiPacket::AnyPartParamValues& _values) const;
    std::string getCategory(const pluginLib::MidiPacket::AnyPartParamValues& _values) const;
    std::string getString(const pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _prefix, size_t _len) const;

    bool setSingleName(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _value) const;
    bool setCategory(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _value) const;
    bool setString(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _prefix, size_t _len, const std::string& _value) const;

private:
	void selectPreset(int _offset);

    static std::string loadParameterDescriptions();

    void onStateLoaded() override;

    void applyPatchParameters(const pluginLib::MidiPacket::ParamValues& _params, uint8_t _part) const;
    void parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params);
    void parseMulti(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params);
    bool parseMidiPacket(MidiPacketType _type, pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _params, const pluginLib::SysEx& _sysex) const;

    bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource _source) override;
	bool parseControllerMessage(const synthLib::SMidiEvent&) override;

	void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;
    bool sendGlobalParameterChange(mqLib::GlobalParameter _param, uint8_t _value);
	void requestSingle(mqLib::MidiBufferNum _buf, mqLib::MidiSoundLocation _location, uint8_t _locationOffset = 0) const;
	void requestMulti(mqLib::MidiBufferNum _buf, mqLib::MidiSoundLocation _location, uint8_t _locationOffset = 0) const;

    uint8_t getGlobalParam(mqLib::GlobalParameter _type) const;

	bool isDerivedParameter(pluginLib::Parameter& _derived, pluginLib::Parameter& _base) const override;

	void requestAllPatches() const;

    const uint8_t m_deviceId;

    Patch m_singleEditBuffer;
    std::array<Patch,16> m_singleEditBuffers;
    std::array<uint8_t, 200> m_globalData{};
    mqJucePlugin::FrontPanel* m_frontPanel = nullptr;
    std::array<uint32_t, 16> m_currentSingles{0};
    uint32_t m_currentSingle = 0;
};
