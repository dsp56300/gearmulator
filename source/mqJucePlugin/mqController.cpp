#include "mqController.h"

#include <fstream>

#include "mqEditor.h"
#include "PluginProcessor.h"

#include "../synthLib/os.h"

constexpr const char* g_midiPacketNames[] =
{
    "requestsingle",
    "requestmulti",
    "requestdrum",
    "requestsinglebank",
    "requestmultibank",
    "requestdrumbank",
    "requestglobal",
    "requestallsingles",
    "singleparameterchange",
    "singledump"
};

static_assert(std::size(g_midiPacketNames) == static_cast<size_t>(Controller::MidiPacketType::Count));

static const char* midiPacketName(Controller::MidiPacketType _type)
{
	return g_midiPacketNames[static_cast<uint32_t>(_type)];
}

Controller::Controller(AudioPluginAudioProcessor& p, unsigned char _deviceId) : pluginLib::Controller(loadParameterDescriptions()), m_processor(p), m_deviceId(_deviceId)
{
    registerParams(p);
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

std::string Controller::loadParameterDescriptions()
{
    const auto name = "parameterDescriptions_mq.json";
    const auto path = synthLib::getModulePath() +  name;

    const std::ifstream f(path.c_str(), std::ios::in);
    if(f.is_open())
    {
		std::stringstream buf;
		buf << f.rdbuf();
        return buf.str();
    }

    uint32_t size;
    const auto res = mqJucePlugin::Editor::findNamedResourceByFilename(name, size);
    if(res)
        return {res, size};
    return {};
}
