#include "xtController.h"

#include <fstream>

#include "BinaryData.h"
#include "PluginProcessor.h"

#include "xtLib/xtState.h"

#include "synthLib/os.h"

#include "dsp56kEmu/logging.h"

#include "xtFrontPanel.h"
#include "xtWaveEditor.h"

namespace
{
	constexpr const char* g_midiPacketNames[] =
	{
	    "requestsingle",
	    "requestmulti",
	    "requestsinglebank",
	    "requestmultibank",
	    "requestglobal",
	    "requestmode",
	    "requestallsingles",
	    "singleparameterchange",
	    "multiparameterchange",
	    "globalparameterchange",
	    "singledump",
	    "multidump",
	    "globaldump",
	    "modedump",
	    "emuRequestLcd",
	    "emuRequestLeds",
	    "emuSendButton",
	    "emuSendRotary",
	    "requestWave",
	    "waveDump",
		"requestTable",
		"tableDump"
	};

	static_assert(std::size(g_midiPacketNames) == static_cast<size_t>(xtJucePlugin::Controller::MidiPacketType::Count));

	const char* midiPacketName(xtJucePlugin::Controller::MidiPacketType _type)
	{
		return g_midiPacketNames[static_cast<uint32_t>(_type)];
	}

	constexpr uint32_t g_pageMulti = 10;
	constexpr uint32_t g_pageMultiInst0 = 11;
	constexpr uint32_t g_pageMultiInst1 = 12;
	constexpr uint32_t g_pageMultiInst2 = 13;
	constexpr uint32_t g_pageMultiInst3 = 14;
	constexpr uint32_t g_pageMultiInst4 = 15;
	constexpr uint32_t g_pageMultiInst5 = 16;
	constexpr uint32_t g_pageMultiInst6 = 17;
	constexpr uint32_t g_pageMultiInst7 = 18;
	constexpr uint32_t g_pageGlobal = 20;
	constexpr uint32_t g_pageSoftKnobs = 30;
	constexpr uint32_t g_pageControllers = 40;
}

namespace xtJucePlugin
{
	Controller::Controller(AudioPluginAudioProcessor& p, const unsigned char _deviceId) : pluginLib::Controller(p, "parameterDescriptions_xt.json"), m_deviceId(_deviceId)
	{
	    registerParams(p);

		sendSysEx(RequestGlobal);
		sendSysEx(RequestMode);

		onPlayModeChanged.addListener([this](bool multiMode)
		{
			requestAllPatches();
		});

		// slow down edits of the wavetable, device gets overloaded quickly if we send too many changes
		uint32_t idx;
		getParameterDescriptions().getIndexByName(idx, "Wave");
		for(uint8_t i=0; i<m_singleEditBuffers.size(); ++i)
		{
			auto* p = getParameter(idx, i);
			p->setRateLimitMilliseconds(250);
		}
	}

	Controller::~Controller() = default;

	bool Controller::sendSingle(const std::vector<uint8_t>& _sysex)
	{
		return sendSingle(_sysex, getCurrentPart());
	}

	bool Controller::sendSingle(const std::vector<uint8_t>& _sysex, const uint8_t _part)
	{
		if(_sysex.size() == xt::Mw1::g_singleDumpLength)
		{
			// No program/bank bytes are part of the dump, send as-is and request the result

			if(_part > 0)
				return false;	// we cannot support this as the hardware loads a MW1 to the "current" instrument, which is always the first one

			pluginLib::Controller::sendSysEx(_sysex);
			requestSingle(isMultiMode() ? xt::LocationH::SingleEditBufferMultiMode : xt::LocationH::SingleEditBufferSingleMode, 0);
			return true;
		}

		auto data = _sysex;

		if(_sysex.size() > std::tuple_size_v<xt::State::Single>)
		{
			// split a combined single into single, table, waves and send all of them
			std::vector<xt::SysEx> splitResults;
			xt::State::splitCombinedPatch(splitResults, _sysex);

			if(!splitResults.empty())
			{
				const auto& single = splitResults.front();

				if(m_waveEditor)
				{
					// send waves first, then table. otherwise the device doesn't refresh correctly
					const auto& table = splitResults[1];

					for(size_t i=2; i<splitResults.size(); ++i)
					{
						const auto& wave = splitResults[i];
						m_waveEditor->getData().onReceiveWave(wave, true);
					}

					m_waveEditor->getData().onReceiveTable(table, true);
				}

				data = single;
			}
		}

		data[wLib::IdxBuffer] = static_cast<uint8_t>(isMultiMode() ? xt::LocationH::SingleEditBufferMultiMode : xt::LocationH::SingleEditBufferSingleMode);
		data[wLib::IdxLocation] = isMultiMode() ? _part : 0;
		data[wLib::IdxDeviceId] = m_deviceId;

		const auto* p = getMidiPacket(g_midiPacketNames[SingleDump]);

		if (!p->updateChecksums(data))
			return false;

		pluginLib::Controller::sendSysEx(data);

		sendLockedParameters(_part);

		requestSingle(isMultiMode() ? xt::LocationH::SingleEditBufferMultiMode : xt::LocationH::SingleEditBufferSingleMode, 0);

		return true;
	}

	void Controller::onStateLoaded()
	{
		sendSysEx(RequestGlobal);
		sendSysEx(RequestMode);
	}

	uint8_t Controller::getPartCount() const
	{
		return 8;
	}

	std::string Controller::getSingleName(const pluginLib::MidiPacket::ParamValues& _values) const
	{
	    std::string name;
	    for(uint32_t i=0; i<16; ++i)
	    {
	        char paramName[16];
	        (void)snprintf(paramName, sizeof(paramName), "Name%02u", i);
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

	std::string Controller::getSingleName(const uint8_t _part) const
	{
	    std::string name;
	    for(uint32_t i=0; i<16; ++i)
	    {
	        char paramName[16];
	        (void)snprintf(paramName, sizeof(paramName), "Name%02u", i);
	        const auto idx = getParameterIndexByName(paramName);
	        if(idx == InvalidParameterIndex)
	            break;

			const auto* p = getParameter(idx, _part);
	        if(!p)
	            break;

	        name += static_cast<char>(p->getUnnormalizedValue());
	    }
	    return name;
	}

	std::string Controller::getSingleName(const pluginLib::MidiPacket::AnyPartParamValues& _values) const
	{
	    return getString(_values, "Name", 16);
	}

	std::string Controller::getString(const pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _prefix,	const size_t _len) const
	{
	    std::string name;
	    for(uint32_t i=0; i<_len; ++i)
	    {
	        char paramName[64];
	        (void)snprintf(paramName, sizeof(paramName), "%s%02u", _prefix.c_str(), i);

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

	void Controller::parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params)
	{
	    Patch patch;
	    patch.data = _msg;
	    patch.name = getSingleName(_params);

	    const auto bank = _data.at(pluginLib::MidiDataType::Bank);
	    const auto prog = _data.at(pluginLib::MidiDataType::Program);

	    if(bank == static_cast<uint8_t>(xt::LocationH::SingleEditBufferSingleMode) && prog == 0)
	    {
		    m_singleEditBuffer = patch;

			if(!isMultiMode())
				applyPatchParameters(_params, 0);
	    }
	    else if(bank == static_cast<uint8_t>(xt::LocationH::SingleEditBufferMultiMode))
	    {
		    m_singleEditBuffers[prog] = patch;

    		if (isMultiMode())
				applyPatchParameters(_params, prog);

			// if we switched to multi, all singles have to be requested. However, we cannot send all requests at once (device will miss some)
			// so we chain them one after the other
			if(prog + 1 < m_singleEditBuffers.size())
				requestSingle(xt::LocationH::SingleEditBufferMultiMode, prog + 1);
	    }

		onProgramChanged(prog);
	}

	void Controller::parseMulti(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data,	const pluginLib::MidiPacket::ParamValues& _params) const
	{
		Patch patch;
		patch.data = _msg;
		patch.name = getSingleName(_params);

		const auto bank = _data.at(pluginLib::MidiDataType::Bank);
	//	const auto prog = _data.at(pluginLib::MidiDataType::Program);

		if(bank == static_cast<uint8_t>(xt::LocationH::MultiDumpMultiEditBuffer))
		{
			applyPatchParameters(_params, 0);

			if(isMultiMode())
			{
				// requst first single. The other singles 1-7 are requested one after the other after a single has been received
				requestSingle(xt::LocationH::SingleEditBufferMultiMode, 0);
			}
		}
	}

	void Controller::parseGlobal(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params)
	{
		memcpy(m_globalData.data(), &_msg[xt::IdxGlobalParamFirst], sizeof(m_globalData));

		applyPatchParameters(_params, 0);
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx& _msg, synthLib::MidiEventSource)
	{
	    if(_msg.size() >= 5)
	    {
		    switch (const auto cmd = static_cast<xt::SysexCommand>(_msg[4]))
	        {
	        case xt::SysexCommand::EmuRotaries:
	        case xt::SysexCommand::EmuButtons:
	        case xt::SysexCommand::EmuLCD:
	        case xt::SysexCommand::EmuLEDs:
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
		    parseGlobal(_msg, data, parameterValues);
	    }
	    else if(name == midiPacketName(ModeDump))
	    {
		    const auto lastPlayMode = isMultiMode();
		    memcpy(m_modeData.data(), &_msg[xt::IdxModeParamFirst], sizeof(m_modeData));
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
		    }
	    }
		else if(name == midiPacketName(WaveDump))
		{
			if(m_waveEditor)
				m_waveEditor->onReceiveWave(data, _msg);
		}
		else if(name == midiPacketName(TableDump))
		{
			if(m_waveEditor)
				m_waveEditor->onReceiveTable(data, _msg);
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
	    return m_modeData.front() != 0;
	}

	void Controller::setPlayMode(const bool _multiMode)
	{
		if(isMultiMode() == _multiMode)
			return;

		m_modeData[0] = _multiMode ? 1 : 0;

		sendModeDump();

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

	std::vector<uint8_t> Controller::createSingleDump(const xt::LocationH _buffer, const uint8_t _location, const uint8_t _part) const
	{
		pluginLib::MidiPacket::Data data;

		data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
		data.insert(std::make_pair(pluginLib::MidiDataType::Bank, static_cast<uint8_t>(_buffer)));
		data.insert(std::make_pair(pluginLib::MidiDataType::Program, _location));

		std::vector<uint8_t> dst;

		if (!createMidiDataFromPacket(dst, midiPacketName(SingleDump), data, _part))
			return {};

		return dst;
	}

	std::vector<uint8_t> Controller::createSingleDump(xt::LocationH _buffer, const uint8_t _location, const pluginLib::MidiPacket::AnyPartParamValues& _values) const
	{
		pluginLib::MidiPacket::Data data;

		data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
		data.insert(std::make_pair(pluginLib::MidiDataType::Bank, static_cast<uint8_t>(_buffer)));
		data.insert(std::make_pair(pluginLib::MidiDataType::Program, _location));

		std::vector<uint8_t> dst;

		if (!createMidiDataFromPacket(dst, midiPacketName(SingleDump), data, _values))
			return {};

		return dst;
	}

	bool Controller::parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _paramValues, const std::vector<uint8_t>& _sysex) const
	{
		return parseMidiPacket(SingleDump, _data, _paramValues, _sysex);
	}

	bool Controller::setString(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _prefix, size_t _len, const std::string& _value) const
	{
	    for(uint32_t i=0; i<_len && i <_value.size(); ++i)
	    {
	        char paramName[64];
	        (void)snprintf(paramName, sizeof(paramName), "%s%02u", _prefix.c_str(), i);

	        const auto idx = getParameterIndexByName(paramName);
	        if(idx == InvalidParameterIndex)
	            break;

	        _values[idx] = static_cast<uint8_t>(_value[i]);
	    }
	    return true;
	}

	void Controller::setFrontPanel(xtJucePlugin::FrontPanel* _frontPanel)
	{
		m_frontPanel = _frontPanel;
	}

	void Controller::setWaveEditor(xtJucePlugin::WaveEditor* _waveEditor)
	{
		m_waveEditor = _waveEditor;
	}

	void Controller::selectPreset(const int _offset)
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
	        sendMidiEvent(synthLib::M_CONTROLCHANGE, synthLib::MC_BANKSELECTLSB, static_cast<uint8_t>(xt::LocationH::SingleBankA) + bank);
	        sendMidiEvent(synthLib::M_PROGRAMCHANGE, static_cast<uint8_t>(single), 0);
	/*
			sendGlobalParameterChange(xt::GlobalParameter::InstrumentABankNumber, static_cast<uint8_t>(bank));
		    sendGlobalParameterChange(xt::GlobalParameter::InstrumentASingleNumber, static_cast<uint8_t>(single));
	*/  }
	}

	void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, const pluginLib::ParamValue _value)
	{
		const auto &desc = _parameter.getDescription();

		std::map<pluginLib::MidiDataType, uint8_t> data;

		switch (desc.page)
		{
		case g_pageGlobal:
			{
				data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, _parameter.getDescription().index & 0x7f));
				data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, _value));

				sendSysEx(GlobalParameterChange, data);
			}
			return;
		case g_pageMulti:
		case g_pageMultiInst0:
		case g_pageMultiInst1:
		case g_pageMultiInst2:
		case g_pageMultiInst3:
		case g_pageMultiInst4:
		case g_pageMultiInst5:
		case g_pageMultiInst6:
		case g_pageMultiInst7:
			{
				uint8_t v;

				if (!combineParameterChange(v, g_midiPacketNames[MultiDump], _parameter, _value))
					return;

				uint8_t page;

				if(desc.page > g_pageMulti)
					page = desc.page - g_pageMultiInst0;
				else
					page = static_cast<uint8_t>(xt::LocationH::MultiDumpMultiEditBuffer);

				data.insert(std::make_pair(pluginLib::MidiDataType::Part, _parameter.getPart()));
				data.insert(std::make_pair(pluginLib::MidiDataType::Page, page));
				data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, desc.index));
				data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, v));

				sendSysEx(MultiParameterChange, data);
			}
			return;
		case g_pageSoftKnobs:
			break;
		case g_pageControllers:
			break;
		default:
			{
				uint8_t v;
				if (!combineParameterChange(v, g_midiPacketNames[SingleDump], _parameter, _value))
					return;

				data.insert(std::make_pair(pluginLib::MidiDataType::Part, _parameter.getPart()));
				data.insert(std::make_pair(pluginLib::MidiDataType::Page, desc.page));
				data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, desc.index));
				data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, v));

				sendSysEx(SingleParameterChange, data);
			}
			break;
		}
	}

	bool Controller::sendGlobalParameterChange(xt::GlobalParameter _param, uint8_t _value)
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

	bool Controller::sendModeDump() const
	{
		std::vector<uint8_t> sysex;
		std::map<pluginLib::MidiDataType, uint8_t> data;
		data.insert({pluginLib::MidiDataType::DeviceId, m_deviceId});
		if(!createMidiDataFromPacket(sysex, midiPacketName(ModeDump), data, 0))
			return false;
		sysex[xt::IdxModeParamFirst] = m_modeData.front();
		pluginLib::Controller::sendSysEx(sysex);
		return true;
	}

	void Controller::requestSingle(xt::LocationH _buf, uint8_t _location) const
	{
		std::map<pluginLib::MidiDataType, uint8_t> params;
	    params[pluginLib::MidiDataType::Bank] = static_cast<uint8_t>(_buf);
	    params[pluginLib::MidiDataType::Program] = _location;
	    sendSysEx(RequestSingle, params);
	}

	void Controller::requestMulti(xt::LocationH _buf, uint8_t _location) const
	{
		std::map<pluginLib::MidiDataType, uint8_t> params;
		params[pluginLib::MidiDataType::Bank] = static_cast<uint8_t>(_buf);
		params[pluginLib::MidiDataType::Program] = _location;
		sendSysEx(RequestMulti, params);
	}

	bool Controller::requestWave(const uint32_t _number) const
	{
		if(!xt::wave::isValidWaveIndex(_number))
			return false;

		std::map<pluginLib::MidiDataType, uint8_t> params;

		params[pluginLib::MidiDataType::Bank] = static_cast<uint8_t>(_number >> 7);
		params[pluginLib::MidiDataType::Program] = _number & 0x7f;

		return sendSysEx(RequestWave, params);
	}

	bool Controller::requestTable(const uint32_t _number) const
	{
		if(!xt::wave::isValidTableIndex(_number))
			return false;

		std::map<pluginLib::MidiDataType, uint8_t> params;

		params[pluginLib::MidiDataType::Bank] = static_cast<uint8_t>(_number >> 7);
		params[pluginLib::MidiDataType::Program] = _number & 0x7f;

		return sendSysEx(RequestTable, params);
	}

	uint8_t Controller::getGlobalParam(xt::GlobalParameter _type) const
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
			requestMulti(xt::LocationH::MultiDumpMultiEditBuffer, 0);
		}
		else
		{
			requestSingle(xt::LocationH::SingleEditBufferSingleMode, 0);
		}
	}
}