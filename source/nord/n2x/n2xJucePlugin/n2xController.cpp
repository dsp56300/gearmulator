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

	static_assert(std::size(g_midiPacketNames) == static_cast<size_t>(Controller::MidiPacketType::Count));

	const char* midiPacketName(Controller::MidiPacketType _type)
	{
		return g_midiPacketNames[static_cast<uint32_t>(_type)];
	}
}

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
	return false;
}

bool Controller::parseControllerMessage(const synthLib::SMidiEvent&)
{
	// TODO
	return false;
}

void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, const uint8_t _value)
{
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
