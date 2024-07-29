#include "n2xController.h"

#include <fstream>

#include "BinaryData.h"
#include "n2xPluginProcessor.h"

#include "synthLib/os.h"

#include "dsp56kEmu/logging.h"

namespace
{
}

Controller::Controller(AudioPluginAudioProcessor& _p) : pluginLib::Controller(_p, loadParameterDescriptions())
{
    registerParams(_p);
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
