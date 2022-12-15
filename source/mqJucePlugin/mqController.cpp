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
    "globalparameterchange",
    "singledump",
    "globaldump"
};

static_assert(std::size(g_midiPacketNames) == static_cast<size_t>(Controller::MidiPacketType::Count));

static const char* midiPacketName(Controller::MidiPacketType _type)
{
	return g_midiPacketNames[static_cast<uint32_t>(_type)];
}

Controller::Controller(AudioPluginAudioProcessor& p, unsigned char _deviceId) : pluginLib::Controller(p, loadParameterDescriptions()), m_processor(p), m_deviceId(_deviceId)
{
    registerParams(p);

//  sendSysEx(RequestAllSingles);
	sendSysEx(RequestGlobal);
    sendGlobalParameterChange(mqLib::GlobalParameter::SingleMultiMode, 1);
    requestSingle(mqLib::MidiBufferNum::EditBufferSingle, mqLib::MidiSoundLocation::EditBufferCurrentSingle);

    startTimer(50);
}

Controller::~Controller() = default;

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

void Controller::timerCallback()
{
    std::vector<synthLib::SMidiEvent> events;
    getPluginMidiOut(events);

    for (const auto& e : events)
    {
	    if(!e.sysex.empty())
            parseSysexMessage(e.sysex);
    }
}

void Controller::onStateLoaded()
{
}

std::string Controller::getSingleName(const pluginLib::MidiPacket::ParamValues& _values) const
{
    std::string name;
    for(uint32_t i=0; i<16; ++i)
    {
        char paramName[16];
        sprintf(paramName, "Name%02u", i);
        const auto idx = getParameterIndexByName(paramName);
        if(idx == InvalidParameterIndex)
            break;

        const auto it = _values.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idx));
        if(it == _values.end())
            break;

        name += static_cast<char>(it->second);
    }
    return name;
}

void Controller::parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params)
{
    Patch patch;
    patch.data = _msg;
    patch.name = getSingleName(_params);

    const auto bank = _data.at(pluginLib::MidiDataType::Bank);
    const auto prog = _data.at(pluginLib::MidiDataType::Program);

    if(bank == static_cast<uint8_t>(mqLib::MidiBufferNum::EditBufferSingle) && prog == static_cast<uint8_t>(mqLib::MidiSoundLocation::EditBufferCurrentSingle))
    {
	    m_singleEditBuffer = patch;

        for(auto it = _params.begin(); it != _params.end(); ++it)
        {
            auto* p = getParameter(it->first.second, 0);
			p->setValueFromSynth(it->second, true, pluginLib::Parameter::ChangedBy::PresetChange);

            for (const auto& derivedParam : p->getDerivedParameters())
	            derivedParam->setValueFromSynth(it->second, true, pluginLib::Parameter::ChangedBy::PresetChange);
        }
    }
}

void Controller::parseSysexMessage(const pluginLib::SysEx& _msg)
{
    LOG("Got sysex of size " << _msg.size())

	std::string name;
    pluginLib::MidiPacket::Data data;
    pluginLib::MidiPacket::ParamValues parameterValues;

    if(parseMidiPacket(name,  data, parameterValues, _msg))
    {
        if(name == midiPacketName(SingleDump))
        {
	        parseSingle(_msg, data, parameterValues);
        }
        if(name == midiPacketName(GlobalDump))
        {
            memcpy(&m_globalData[0], &_msg[5], sizeof(m_globalData));
        }
        else
        {
	        LOG("Received unknown sysex of size " << _msg.size());
        }
    }
}

bool Controller::sendSysEx(MidiPacketType _type) const
{
    std::map<pluginLib::MidiDataType, uint8_t> params;
    return sendSysEx(_type, params);
}

bool Controller::sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const
{
    _params.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
    return pluginLib::Controller::sendSysEx(midiPacketName(_type), _params);
}

void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, const uint8_t _value)
{
    const auto& desc = _parameter.getDescription();

    sendParameterChange(desc.page, _parameter.getPart(), desc.index, _value);
}

bool Controller::sendParameterChange(uint8_t _page, uint8_t _part, uint8_t _index, uint8_t _value) const
{
    std::map<pluginLib::MidiDataType, uint8_t> data;

    data.insert(std::make_pair(pluginLib::MidiDataType::Part, _part));
    data.insert(std::make_pair(pluginLib::MidiDataType::Page, _page));
    data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, _index));
    data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, _value));

    return sendSysEx(SingleParameterChange, data);
}

bool Controller::sendGlobalParameterChange(mqLib::GlobalParameter _param, uint8_t _value) const
{
    std::map<pluginLib::MidiDataType, uint8_t> data;

    const auto index = static_cast<uint32_t>(_param);

    data.insert(std::make_pair(pluginLib::MidiDataType::Page, index >> 7 ));
    data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, index & 0x7f ));
    data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, _value));

    return sendSysEx(GlobalParameterChange, data);
}

void Controller::requestSingle(mqLib::MidiBufferNum _buf, mqLib::MidiSoundLocation _location) const
{
	std::map<pluginLib::MidiDataType, uint8_t> params;
    params[pluginLib::MidiDataType::Bank] = static_cast<uint8_t>(_buf);
    params[pluginLib::MidiDataType::Program] = static_cast<uint8_t>(_location);
    sendSysEx(RequestSingle, params);
}

uint8_t Controller::getGlobalParam(mqLib::GlobalParameter _type) const
{
    return m_globalData[static_cast<uint32_t>(_type)];
}
