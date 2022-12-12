#pragma once

#include "../jucePluginLib/controller.h"

class AudioPluginAudioProcessor;

class Controller : public pluginLib::Controller, juce::Timer
{
public:
    Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);
	~Controller() override;
private:
};
