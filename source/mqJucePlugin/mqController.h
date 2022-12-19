#pragma once

#include "../jucePluginLib/controller.h"

#include "../mqLib/mqmiditypes.h"

class FrontPanel;
class AudioPluginAudioProcessor;

class Controller : public pluginLib::Controller, juce::Timer
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
        GlobalParameterChange,
        SingleDump,
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

    Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
	~Controller() override;

    void setFrontPanel(FrontPanel* _frontPanel);

	bool sendSysEx(MidiPacketType _type) const;
    bool sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

private:
    static std::string loadParameterDescriptions();

	void timerCallback() override;
    void onStateLoaded() override;

    std::string getSingleName(const pluginLib::MidiPacket::ParamValues& _values) const;
    void parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params);
    void parseSysexMessage(const pluginLib::SysEx&) override;

	void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;
    bool sendParameterChange(uint8_t _page, uint8_t _part, uint8_t _index, uint8_t _value) const;
    bool sendGlobalParameterChange(mqLib::GlobalParameter _param, uint8_t _value);
	void requestSingle(mqLib::MidiBufferNum _buf, mqLib::MidiSoundLocation _location) const;

    uint8_t getGlobalParam(mqLib::GlobalParameter _type) const;

    const uint8_t m_deviceId;

    Patch m_singleEditBuffer;
    std::array<uint8_t, 200> m_globalData{};
    FrontPanel* m_frontPanel = nullptr;
};
