#pragma once

#include "../jucePluginLib/controller.h"

#include "../mqLib/mqmiditypes.h"

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
        SingleDump,

        Count
    };

    struct Patch
    {
        std::string name;
		std::vector<uint8_t> data;
    };

    Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
	~Controller() override;

private:
    void timerCallback() override;
    void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;
    std::string getSingleName(const pluginLib::MidiPacket::ParamValues& _values);
    void parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params);
    void parseSysexMessage(const pluginLib::SysEx&) override;
    void onStateLoaded() override;
    static std::string loadParameterDescriptions();
    bool sendSysEx(MidiPacketType _type) const;
    bool sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

    void requestSingle(mqLib::MidiBufferNum _buf, mqLib::MidiSoundLocation _location) const;
    AudioPluginAudioProcessor& m_processor;
    const uint8_t m_deviceId;

    Patch m_singleEditBuffer;
};
