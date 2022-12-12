#pragma once

#include "../jucePluginLib/controller.h"

class AudioPluginAudioProcessor;

class Controller : public pluginLib::Controller, juce::Timer
{
public:
    Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
	~Controller() override;
private:
    void timerCallback() override;
    void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;
    void parseSysexMessage(const pluginLib::SysEx&) override;
    void onStateLoaded() override;
};
