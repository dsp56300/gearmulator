#include "mqController.h"

#include <fstream>

#include "mqEditor.h"
#include "PluginProcessor.h"

#include "../synthLib/os.h"

#pragma optimize("", off)

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
    "singledump_Q",
    "globaldump",
    "emuRequestLcd",
    "emuRequestLeds",
    "emuSendButton",
    "emuSendRotary"
};

static_assert(std::size(g_midiPacketNames) == static_cast<size_t>(Controller::MidiPacketType::Count));

static const char* midiPacketName(Controller::MidiPacketType _type)
{
	return g_midiPacketNames[static_cast<uint32_t>(_type)];
}

Controller::Controller(AudioPluginAudioProcessor& p, unsigned char _deviceId) : pluginLib::Controller(p, loadParameterDescriptions()), m_deviceId(_deviceId)
{
    registerParams(p);

//  sendSysEx(RequestAllSingles);
	sendSysEx(RequestGlobal);
//  sendGlobalParameterChange(mqLib::GlobalParameter::SingleMultiMode, 1);

    startTimer(50);
}

Controller::~Controller() = default;

void Controller::setFrontPanel(mqJucePlugin::FrontPanel* _frontPanel)
{
    m_frontPanel = _frontPanel;
}

void Controller::sendSingle(const std::vector<uint8_t>& _sysex)
{
	auto data = _sysex;

	data[mqLib::IdxBuffer] = static_cast<uint8_t>(isMultiMode() ? mqLib::MidiBufferNum::SingleEditBufferMultiMode : mqLib::MidiBufferNum::SingleEditBufferSingleMode);
	data[mqLib::IdxLocation] = isMultiMode() ? getCurrentPart() : 0;

	const auto* p = getMidiPacket(g_midiPacketNames[SingleDump]);

	if (!p->updateChecksums(data))
	{
		p = getMidiPacket(g_midiPacketNames[SingleDumpQ]);

		if(!p->updateChecksums(data))
			return;
	}

	pluginLib::Controller::sendSysEx(data);
    parseSysexMessage(data);
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
    const auto res = mqJucePlugin::Editor::findEmbeddedResource(name, size);
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

    if(bank == static_cast<uint8_t>(mqLib::MidiBufferNum::SingleEditBufferSingleMode) && prog == static_cast<uint8_t>(mqLib::MidiSoundLocation::EditBufferCurrentSingle))
    {
	    m_singleEditBuffer = patch;

        for (const auto& it : _params)
        {
            auto* p = getParameter(it.first.second, 0);
			p->setValueFromSynth(it.second, true, pluginLib::Parameter::ChangedBy::PresetChange);

            for (const auto& derivedParam : p->getDerivedParameters())
	            derivedParam->setValueFromSynth(it.second, true, pluginLib::Parameter::ChangedBy::PresetChange);
        }
    }
    else if(bank == static_cast<uint8_t>(mqLib::MidiBufferNum::SingleEditBufferMultiMode))
    {
	    m_singleEditBuffers[prog] = patch;
    }
}

void Controller::parseSysexMessage(const pluginLib::SysEx& _msg)
{
    if(_msg.size() >= 5)
    {
        const auto cmd = static_cast<mqLib::SysexCommand>(_msg[4]);
        switch (cmd)
        {
        case mqLib::SysexCommand::EmuRotaries:
        case mqLib::SysexCommand::EmuButtons:
        case mqLib::SysexCommand::EmuLCD:
        case mqLib::SysexCommand::EmuLCDCGRata:
        case mqLib::SysexCommand::EmuLEDs:
            if(m_frontPanel)
                m_frontPanel->processSysex(_msg);
            return;
        default:
            break;
        }
    }

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
        else if(name == midiPacketName(GlobalDump))
        {
            memcpy(m_globalData.data(), &_msg[5], sizeof(m_globalData));

            if(isMultiMode())
            {
			    for(uint8_t i=0; i<16; ++i)
				    requestSingle(mqLib::MidiBufferNum::SingleEditBufferMultiMode, mqLib::MidiSoundLocation::EditBufferFirstMultiSingle, i);
            }
            else
            {
				requestSingle(mqLib::MidiBufferNum::SingleEditBufferSingleMode, mqLib::MidiSoundLocation::EditBufferCurrentSingle);
            }
        }
        else if(name == midiPacketName(SingleParameterChange))
        {
            const auto page = data[pluginLib::MidiDataType::Page];
            const auto index = data[pluginLib::MidiDataType::ParameterIndex];
            const auto part = data[pluginLib::MidiDataType::Part];
            const auto value = data[pluginLib::MidiDataType::ParameterValue];

        	auto& params = findSynthParam(part, page, index);

            for (auto& param : params)
	            param->setValueFromSynth(value, true, pluginLib::Parameter::ChangedBy::ControlChange);

            LOG("Single parameter " << static_cast<int>(index) << ", page " << static_cast<int>(page) << " for part " << static_cast<int>(part) << " changed to value " << static_cast<int>(value));
        }
        else if(name == midiPacketName(GlobalParameterChange))
        {
            const auto index = (static_cast<uint32_t>(data[pluginLib::MidiDataType::Page]) << 7) + static_cast<uint32_t>(data[pluginLib::MidiDataType::ParameterIndex]);
            const auto value = data[pluginLib::MidiDataType::ParameterValue];

            LOG("Global parameter " << index << " changed to value " << static_cast<int>(value));
            m_globalData[index] = value;
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

bool Controller::sendSysEx(const MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const
{
    _params.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
    return pluginLib::Controller::sendSysEx(midiPacketName(_type), _params);
}

bool Controller::isMultiMode() const
{
    return m_globalData[static_cast<uint32_t>(mqLib::GlobalParameter::SingleMultiMode)] != 0;
}

void Controller::setPlayMode(bool _multiMode)
{
}

void Controller::selectNextPreset()
{
	selectPreset(+1);
}

void Controller::selectPrevPreset()
{
	selectPreset(-1);
}

void Controller::selectPreset(int _offset)
{
    auto& current = isMultiMode() ? m_currentSingles[getCurrentPart()] : m_currentSingle;

	int index = static_cast<int>(current) + _offset;

	if (index < 0)
        index += 300;

	if (index >= 300)
        index -= 300;

    current = static_cast<uint32_t>(index);

    const int single = index % 100;
    const int bank = index / 100;

    if (isMultiMode())
    {
	    // TODO: modify multi
    }
	else
    {
		sendMidiEvent(synthLib::M_CONTROLCHANGE, synthLib::MC_BANKSELECTMSB, m_deviceId);
        sendMidiEvent(synthLib::M_CONTROLCHANGE, synthLib::MC_BANKSELECTLSB, static_cast<uint8_t>(mqLib::MidiBufferNum::SingleBankA) + bank);
        sendMidiEvent(synthLib::M_PROGRAMCHANGE, static_cast<uint8_t>(single), 0);
/*
		sendGlobalParameterChange(mqLib::GlobalParameter::InstrumentABankNumber, static_cast<uint8_t>(bank));
	    sendGlobalParameterChange(mqLib::GlobalParameter::InstrumentASingleNumber, static_cast<uint8_t>(single));
*/  }
}

void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, const uint8_t _value)
{
	const auto &desc = _parameter.getDescription();

	if (desc.page >= 100)
	{
		// TODO: multi
		return;
	}

	std::map<pluginLib::MidiDataType, uint8_t> data;

	uint8_t v;
	if (!combineParameterChange(v, g_midiPacketNames[SingleDump], _parameter, _value))
		return;

	data.insert(std::make_pair(pluginLib::MidiDataType::Part, _parameter.getPart()));
	data.insert(std::make_pair(pluginLib::MidiDataType::Page, desc.page));
	data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, desc.index));
	data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, v));

	sendSysEx(SingleParameterChange, data);
}

bool Controller::sendGlobalParameterChange(mqLib::GlobalParameter _param, uint8_t _value)
{
    std::map<pluginLib::MidiDataType, uint8_t> data;

    const auto index = static_cast<uint32_t>(_param);

    data.insert(std::make_pair(pluginLib::MidiDataType::Page, index >> 7 ));
    data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, index & 0x7f ));
    data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, _value));

    m_globalData[index] = _value;

    return sendSysEx(GlobalParameterChange, data);
}

void Controller::requestSingle(mqLib::MidiBufferNum _buf, mqLib::MidiSoundLocation _location, uint8_t _locationOffset/* = 0*/) const
{
	std::map<pluginLib::MidiDataType, uint8_t> params;
    params[pluginLib::MidiDataType::Bank] = static_cast<uint8_t>(_buf);
    params[pluginLib::MidiDataType::Program] = static_cast<uint8_t>(_location) + _locationOffset;
    sendSysEx(RequestSingle, params);
}

uint8_t Controller::getGlobalParam(mqLib::GlobalParameter _type) const
{
    return m_globalData[static_cast<uint32_t>(_type)];
}

bool Controller::isDerivedParameter(pluginLib::Parameter& _derived, pluginLib::Parameter& _base) const
{
	const auto& packetName = g_midiPacketNames[SingleDump];
	const auto* packet = getMidiPacket(packetName);

	if (!packet)
	{
		LOG("Failed to find midi packet " << packetName);
		return true;
	}
	
	const auto* defA = packet->getDefinitionByParameterName(_derived.getDescription().name);
	const auto* defB = packet->getDefinitionByParameterName(_base.getDescription().name);

	if (!defA || !defB)
		return true;

	return defA->doMasksOverlap(*defB);
}
