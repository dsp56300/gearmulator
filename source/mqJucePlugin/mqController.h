#pragma once

#include "../jucePluginLib/controller.h"

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

    Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
	~Controller() override;

private:
    void timerCallback() override;
    void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;
    void parseSysexMessage(const pluginLib::SysEx&) override;
    void onStateLoaded() override;
    static std::string Controller::loadParameterDescriptions();
    bool sendSysEx(MidiPacketType _type) const;
    bool sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

    AudioPluginAudioProcessor& m_processor;
    const uint8_t m_deviceId;
};
