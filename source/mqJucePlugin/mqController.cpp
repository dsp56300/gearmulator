#include "mqController.h"

#include <fstream>

#include "mqEditor.h"
#include "mqFrontPanel.h"
#include "PluginProcessor.h"

#include "mqLib/mqstate.h"

#include "synthLib/os.h"

#include "dsp56kEmu/logging.h"

namespace mqJucePlugin
{
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
	    "multiparameterchange",
	    "globalparameterchange",
	    "singledump",
	    "singledump_Q",
	    "multidump",
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

	Controller::Controller(AudioPluginAudioProcessor& p, unsigned char _deviceId) : pluginLib::Controller(p, "parameterDescriptions_mq.json"), m_deviceId(_deviceId)
	{
	    registerParams(p);

	//  sendSysEx(RequestAllSingles);
		sendSysEx(RequestGlobal);
	//  sendGlobalParameterChange(mqLib::GlobalParameter::SingleMultiMode, 1);

		onPlayModeChanged.addListener(0, [this](bool multiMode)
		{
			requestAllPatches();
		});
	}

	Controller::~Controller() = default;

	void Controller::setFrontPanel(mqJucePlugin::FrontPanel* _frontPanel)
	{
	    m_frontPanel = _frontPanel;
	}

	void Controller::sendSingle(const std::vector<uint8_t>& _sysex)
	{
		sendSingle(_sysex, getCurrentPart());
	}

	void Controller::sendSingle(const std::vector<uint8_t>& _sysex, const uint8_t _part)
	{
		auto data = _sysex;

		data[wLib::IdxBuffer] = static_cast<uint8_t>(isMultiMode() ? mqLib::MidiBufferNum::SingleEditBufferMultiMode : mqLib::MidiBufferNum::SingleEditBufferSingleMode);
		data[wLib::IdxLocation] = isMultiMode() ? _part : 0;
		data[wLib::IdxDeviceId] = m_deviceId;

		const auto* p = getMidiPacket(g_midiPacketNames[SingleDump]);

		if (!p->updateChecksums(data))
		{
			p = getMidiPacket(g_midiPacketNames[SingleDumpQ]);

			if(!p->updateChecksums(data))
				return;
		}

		pluginLib::Controller::sendSysEx(data);

		sendLockedParameters(_part);

		requestSingle(isMultiMode() ? mqLib::MidiBufferNum::SingleEditBufferMultiMode : mqLib::MidiBufferNum::SingleEditBufferSingleMode, 
			isMultiMode() ? mqLib::MidiSoundLocation::EditBufferFirstMultiSingle : mqLib::MidiSoundLocation::EditBufferCurrentSingle);
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

	std::string Controller::getSingleName(const pluginLib::MidiPacket::AnyPartParamValues& _values) const
	{
	    return getString(_values, "Name", 16);
	}

	std::string Controller::getCategory(const pluginLib::MidiPacket::AnyPartParamValues& _values) const
	{
	    return getString(_values, "Category", 4);
	}

	std::string Controller::getString(const pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _prefix,	const size_t _len) const
	{
	    std::string name;
	    for(uint32_t i=0; i<_len; ++i)
	    {
	        char paramName[64];
	        snprintf(paramName, sizeof(paramName), "%s%02u", _prefix.c_str(), i);

	        const auto idx = getParameterIndexByName(paramName);
	        if(idx == InvalidParameterIndex)
	            break;

	        const auto it = _values[idx];
	        if(!it)
	            break;

	        name += static_cast<char>(*it);
	    }
	    return name;
	}

	bool Controller::setSingleName(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _value) const
	{
	    return setString(_values, "Name", 16, _value);
	}

	bool Controller::setCategory(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _value) const
	{
	    return setString(_values, "Category", 4, _value);
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

			if(!isMultiMode())
				applyPatchParameters(_params, 0);
	    }
	    else if(bank == static_cast<uint8_t>(mqLib::MidiBufferNum::SingleEditBufferMultiMode))
	    {
		    m_singleEditBuffers[prog] = patch;

    		if (isMultiMode())
				applyPatchParameters(_params, prog);

			// if we switched to multi, all singles have to be requested. However, we cannot send all requests at once (device will miss some)
			// so we chain them one after the other
			if(prog + 1 < m_singleEditBuffers.size())
				requestSingle(mqLib::MidiBufferNum::SingleEditBufferMultiMode, mqLib::MidiSoundLocation::EditBufferFirstMultiSingle, prog + 1);
	    }
	}

	void Controller::parseMulti(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data,	const pluginLib::MidiPacket::ParamValues& _params)
	{
		Patch patch;
		patch.data = _msg;
		patch.name = getSingleName(_params);

		const auto bank = _data.at(pluginLib::MidiDataType::Bank);
	//	const auto prog = _data.at(pluginLib::MidiDataType::Program);

		if(bank == static_cast<uint8_t>(mqLib::MidiBufferNum::MultiEditBuffer))
		{
			applyPatchParameters(_params, 0);
		}
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx& _msg, synthLib::MidiEventSource _source)
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
	            return true;
	        default:
	            break;
	        }
	    }

		LOG("Got sysex of size " << _msg.size());

		std::string name;
	    pluginLib::MidiPacket::Data data;
	    pluginLib::MidiPacket::ParamValues parameterValues;

	    if(!pluginLib::Controller::parseMidiPacket(name,  data, parameterValues, _msg))
		    return false;

		if(name == midiPacketName(SingleDump))
	    {
		    parseSingle(_msg, data, parameterValues);
	    }
	    else if (name == midiPacketName(MultiDump))
	    {
		    parseMulti(_msg, data, parameterValues);
	    }
	    else if(name == midiPacketName(GlobalDump))
	    {
		    const auto lastPlayMode = isMultiMode();
		    memcpy(m_globalData.data(), &_msg[5], sizeof(m_globalData));
		    const auto newPlayMode = isMultiMode();

		    if(lastPlayMode != newPlayMode)
			    onPlayModeChanged(newPlayMode);
		    else
			    requestAllPatches();
	    }
	    else if(name == midiPacketName(SingleParameterChange))
	    {
		    const auto page = data[pluginLib::MidiDataType::Page];
		    const auto index = data[pluginLib::MidiDataType::ParameterIndex];
		    const auto part = data[pluginLib::MidiDataType::Part];
		    const auto value = data[pluginLib::MidiDataType::ParameterValue];

		    auto& params = findSynthParam(part, page, index);

		    for (auto& param : params)
			    param->setValueFromSynth(value, pluginLib::Parameter::Origin::Midi);

		    LOG("Single parameter " << static_cast<int>(index) << ", page " << static_cast<int>(page) << " for part " << static_cast<int>(part) << " changed to value " << static_cast<int>(value));
	    }
	    else if(name == midiPacketName(GlobalParameterChange))
	    {
		    const auto index = (static_cast<uint32_t>(data[pluginLib::MidiDataType::Page]) << 7) + static_cast<uint32_t>(data[pluginLib::MidiDataType::ParameterIndex]);
		    const auto value = data[pluginLib::MidiDataType::ParameterValue];

		    if(m_globalData[index] != value)
		    {
			    LOG("Global parameter " << index << " changed to value " << static_cast<int>(value));
			    m_globalData[index] = value;

			    if (index == static_cast<uint32_t>(mqLib::GlobalParameter::SingleMultiMode))
				    requestAllPatches();
		    }
	    }
	    else
	    {
		    LOG("Received unknown sysex of size " << _msg.size());
		    return false;
	    }
	    return true;
	}

	bool Controller::parseControllerMessage(const synthLib::SMidiEvent&)
	{
		// TODO
		return false;
	}

	bool Controller::parseMidiPacket(MidiPacketType _type, pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _params, const pluginLib::SysEx& _sysex) const
	{
		const auto* p = getMidiPacket(g_midiPacketNames[_type]);
		assert(p && "midi packet not found");
		return pluginLib::Controller::parseMidiPacket(*p, _data, _params, _sysex);
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

	void Controller::setPlayMode(const bool _multiMode)
	{
		const uint8_t playMode = _multiMode ? 1 : 0;

		if(m_globalData[static_cast<uint32_t>(mqLib::GlobalParameter::SingleMultiMode)] == playMode)
			return;

		sendGlobalParameterChange(mqLib::GlobalParameter::SingleMultiMode, playMode);

		onPlayModeChanged(_multiMode);
	}

	void Controller::selectNextPreset()
	{
		selectPreset(+1);
	}

	void Controller::selectPrevPreset()
	{
		selectPreset(-1);
	}

	std::vector<uint8_t> Controller::createSingleDump(const mqLib::MidiBufferNum _buffer, const mqLib::MidiSoundLocation _location, const uint8_t _locationOffset, const uint8_t _part) const
	{
		pluginLib::MidiPacket::Data data;

		data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
		data.insert(std::make_pair(pluginLib::MidiDataType::Bank, static_cast<uint8_t>(_buffer)));
		data.insert(std::make_pair(pluginLib::MidiDataType::Program, static_cast<uint8_t>(_location) + _locationOffset));

		std::vector<uint8_t> dst;

		if (!createMidiDataFromPacket(dst, midiPacketName(SingleDump), data, _part))
			return {};

		return dst;
	}

	std::vector<uint8_t> Controller::createSingleDump(mqLib::MidiBufferNum _buffer, mqLib::MidiSoundLocation _location, const uint8_t _locationOffset, const pluginLib::MidiPacket::AnyPartParamValues& _values) const
	{
		pluginLib::MidiPacket::Data data;

		data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
		data.insert(std::make_pair(pluginLib::MidiDataType::Bank, static_cast<uint8_t>(_buffer)));
		data.insert(std::make_pair(pluginLib::MidiDataType::Program, static_cast<uint8_t>(_location) + _locationOffset));

		std::vector<uint8_t> dst;

		if (!createMidiDataFromPacket(dst, midiPacketName(SingleDump), data, _values))
			return {};

		return dst;
	}

	bool Controller::parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _paramValues, const std::vector<uint8_t>& _sysex) const
	{
		if(parseMidiPacket(SingleDump, _data, _paramValues, _sysex))
			return true;
		return parseMidiPacket(SingleDumpQ, _data, _paramValues, _sysex);
	}

	bool Controller::setString(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _prefix, size_t _len, const std::string& _value) const
	{
	    for(uint32_t i=0; i<_len && i <_value.size(); ++i)
	    {
	        char paramName[64];
	        snprintf(paramName, sizeof(paramName), "%s%02u", _prefix.c_str(), i);

	        const auto idx = getParameterIndexByName(paramName);
	        if(idx == InvalidParameterIndex)
	            break;

	        _values[idx] = static_cast<uint8_t>(_value[i]);
	    }
	    return true;
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

	void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, const pluginLib::ParamValue _value)
	{
		const auto &desc = _parameter.getDescription();

		std::map<pluginLib::MidiDataType, uint8_t> data;

		if (desc.page >= 100)
		{
			uint8_t v;

			if (!combineParameterChange(v, g_midiPacketNames[MultiDump], _parameter, _value))
				return;

			const auto& dump = mqLib::State::Dumps[static_cast<int>(mqLib::State::DumpType::Multi)];

			uint32_t idx = desc.index;

			if(desc.page > 100)
				idx += (static_cast<uint32_t>(mqLib::MultiParameter::Inst1) - static_cast<uint32_t>(mqLib::MultiParameter::Inst0)) * (desc.page - 101);

			data.insert(std::make_pair(pluginLib::MidiDataType::Part, _parameter.getPart()));
			data.insert(std::make_pair(pluginLib::MidiDataType::Page, idx >> 7));
			data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, idx & 0x7f));
			data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, v));

			sendSysEx(MultiParameterChange, data);
			return;
		}

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
		const auto index = static_cast<uint32_t>(_param);

		if(m_globalData[index] == _value)
			return true;

	    std::map<pluginLib::MidiDataType, uint8_t> data;

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

	void Controller::requestMulti(mqLib::MidiBufferNum _buf, mqLib::MidiSoundLocation _location, uint8_t _locationOffset) const
	{
		std::map<pluginLib::MidiDataType, uint8_t> params;
		params[pluginLib::MidiDataType::Bank] = static_cast<uint8_t>(_buf);
		params[pluginLib::MidiDataType::Program] = static_cast<uint8_t>(_location) + _locationOffset;
		sendSysEx(RequestMulti, params);
	}

	uint8_t Controller::getGlobalParam(mqLib::GlobalParameter _type) const
	{
	    return m_globalData[static_cast<uint32_t>(_type)];
	}

	bool Controller::isDerivedParameter(pluginLib::Parameter& _derived, pluginLib::Parameter& _base) const
	{
		if(_derived.getDescription().page >= 100)
			return false;

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

	void Controller::requestAllPatches() const
	{
		if (isMultiMode())
		{
			requestMulti(mqLib::MidiBufferNum::MultiEditBuffer, mqLib::MidiSoundLocation::EditBufferFirstMultiSingle);

			// the other singles 1-15 are requested one after the other after a single has been received
			requestSingle(mqLib::MidiBufferNum::SingleEditBufferMultiMode, mqLib::MidiSoundLocation::EditBufferFirstMultiSingle, 0);
		}
		else
		{
			requestSingle(mqLib::MidiBufferNum::SingleEditBufferSingleMode, mqLib::MidiSoundLocation::EditBufferCurrentSingle);
		}
	}
}
