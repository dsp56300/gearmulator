#include "mqController.h"

#include "PluginProcessor.h"

Controller::Controller(AudioPluginAudioProcessor &p, unsigned char deviceId) : pluginLib::Controller("parameterDescriptions_mq.json")
{
}

Controller::~Controller() = default;

void Controller::timerCallback()
{
}

void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value)
{
}

void Controller::parseSysexMessage(const pluginLib::SysEx&)
{
}

void Controller::onStateLoaded()
{
}
