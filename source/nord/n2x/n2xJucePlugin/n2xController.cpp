#include "n2xController.h"

#include <fstream>

#include "BinaryData.h"
#include "n2xPluginProcessor.h"

#include "synthLib/os.h"

#include "dsp56kEmu/logging.h"
#include "n2xLib/n2xmiditypes.h"

namespace
{
	constexpr const char* g_midiPacketNames[] =
	{
		"requestdump",
		"singledump",
		"multidump"
	};

	static_assert(std::size(g_midiPacketNames) == static_cast<size_t>(n2xJucePlugin::Controller::MidiPacketType::Count));

	const char* midiPacketName(n2xJucePlugin::Controller::MidiPacketType _type)
	{
		return g_midiPacketNames[static_cast<uint32_t>(_type)];
	}
}

namespace n2xJucePlugin
{
	Controller::Controller(AudioPluginAudioProcessor& _p) : pluginLib::Controller(_p, loadParameterDescriptions())
	{
	    registerParams(_p);

		requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, 0);	// single edit buffers A-D
		requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, 1);
		requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, 2);
		requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, 3);

		requestDump(n2x::SysexByte::MultiRequestBankEditBuffer, 0);		// performance edit buffer
	}

	Controller::~Controller() = default;

	const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size)
	{
		for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
		{
			if (BinaryData::originalFilenames[i] != _filename)
				continue;

			int size = 0;
			const auto res = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
			_size = static_cast<uint32_t>(size);
			return res;
		}
		return nullptr;
	}

	std::string Controller::loadParameterDescriptions()
	{
	    const auto name = "parameterDescriptions_n2x.json";
	    const auto path = synthLib::getModulePath() +  name;

	    const std::ifstream f(path.c_str(), std::ios::in);
	    if(f.is_open())
	    {
			std::stringstream buf;
			buf << f.rdbuf();
	        return buf.str();
	    }
		
	    uint32_t size;
	    const auto res = findEmbeddedResource(name, size);
	    if(res)
	        return {res, size};
	    return {};
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx& _msg, synthLib::MidiEventSource)
	{
		if(_msg.size() == n2x::g_singleDumpSize)
			return parseSingleDump(_msg);
		if(_msg.size() == n2x::g_multiDumpSize)
			return parseMultiDump(_msg);
		return false;
	}

	bool Controller::parseSingleDump(const pluginLib::SysEx& _msg)
	{
		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::ParamValues params;

		if(!parseMidiPacket(midiPacketName(MidiPacketType::SingleDump), data, params, _msg))
			return false;

		const auto bank = data[pluginLib::MidiDataType::Bank];
		const auto program = data[pluginLib::MidiDataType::Program];

		if(bank == n2x::SysexByte::SingleDumpBankEditBuffer && program < getPartCount())
		{
			applyPatchParameters(params, program);
			return true;
		}

		assert(false && "receiving a single for a non-edit-buffer is unexpected");
		return false;
	}

	bool Controller::parseMultiDump(const pluginLib::SysEx& _msg)
	{
		return false;
	}

	bool Controller::parseControllerMessage(const synthLib::SMidiEvent& _e)
	{
		const auto& cm = getParameterDescriptions().getControllerMap();
		const auto paramIndices = cm.getControlledParameters(_e);

		if(paramIndices.empty())
			return false;

		for (const auto paramIndex : paramIndices)
		{
			auto* param = getParameter(paramIndex);
			assert(param && "parameter not found for control change");
			// TODO: part
			param->setValueFromSynth(_e.c, midiEventSourceToParameterOrigin(_e.source));
		}

		return true;
	}

	void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, const uint8_t _value)
	{
		const auto& controllerMap = getParameterDescriptions().getControllerMap();

		const auto& ccs = controllerMap.getControlChanges(synthLib::M_CONTROLCHANGE, _parameter.getParameterIndex());
		if(ccs.empty())
			return;
		sendMidiEvent(synthLib::M_CONTROLCHANGE, ccs.front(), _value);
	}

	bool Controller::sendSysEx(MidiPacketType _packet, const std::map<pluginLib::MidiDataType, uint8_t>& _params) const
	{
		return pluginLib::Controller::sendSysEx(midiPacketName(_packet), _params);
	}

	void Controller::requestDump(const uint8_t _bank, const uint8_t _patch) const
	{
		std::map<pluginLib::MidiDataType, uint8_t> params;

	    params[pluginLib::MidiDataType::DeviceId] = n2x::SysexByte::DefaultDeviceId;
	    params[pluginLib::MidiDataType::Bank] = static_cast<uint8_t>(_bank);
	    params[pluginLib::MidiDataType::Program] = _patch;

		sendSysEx(MidiPacketType::RequestDump, params);
	}

	std::vector<uint8_t> Controller::createSingleDump(uint8_t _bank, uint8_t _program, uint8_t _part) const
	{
		pluginLib::MidiPacket::Data data;

		data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, n2x::SysexByte::DefaultDeviceId));
		data.insert(std::make_pair(pluginLib::MidiDataType::Bank, _bank));
		data.insert(std::make_pair(pluginLib::MidiDataType::Program, _program));

		std::vector<uint8_t> dst;

		if (!createMidiDataFromPacket(dst, midiPacketName(MidiPacketType::SingleDump), data, _part))
			return {};

		return dst;
	}

	bool Controller::activatePatch(const std::vector<uint8_t>& _sysex, const uint32_t _part)
	{
		if(_part >= getPartCount())
			return false;

		const auto isSingle =_sysex.size() == n2x::g_singleDumpSize;
		const auto isMulti = _sysex.size() == n2x::g_multiDumpSize;

		if(!isSingle && !isMulti)
			return false;

		if(isMulti && _part != 0)
			return false;

		auto d = _sysex;

		d[n2x::SysexIndex::IdxMsgType] = isSingle ? n2x::SysexByte::SingleDumpBankEditBuffer : n2x::SysexByte::MultiDumpBankEditBuffer;
		d[n2x::SysexIndex::IdxMsgSpec] = static_cast<uint8_t>(_part);

		pluginLib::Controller::sendSysEx(d);

		if(isSingle)
			requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, static_cast<uint8_t>(_part));
		else
			requestDump(n2x::SysexByte::MultiRequestBankEditBuffer, 0);

		return true;
	}

	bool Controller::isDerivedParameter(pluginLib::Parameter& _derived, pluginLib::Parameter& _base) const
	{
		if(_base.getDescription().isNonPartSensitive() || _derived.getDescription().isNonPartSensitive())
			return false;

		if(_derived.getParameterIndex() != _base.getParameterIndex())
			return false;

		const auto& packetName = midiPacketName(MidiPacketType::SingleDump);
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
}
