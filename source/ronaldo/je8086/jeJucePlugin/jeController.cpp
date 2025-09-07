#include "jeController.h"

#include "jePluginProcessor.h"

namespace jeJucePlugin
{
	Controller::Controller(AudioPluginAudioProcessor& _p) : pluginLib::Controller(_p, "parameterDescriptions_je.json")
	{
	    registerParams(_p, [](const uint8_t _part, const bool _isNonPartExclusive)
	    {
			if(_isNonPartExclusive)
				return juce::String();
			char temp[2] = {static_cast<char>('A' + _part),0};
		    return juce::String(temp);
	    });

		Controller::onStateLoaded();
	}

	Controller::~Controller() = default;

	void Controller::onStateLoaded()
	{
//		requestDump(je::SysexByte::SingleRequestBankEditBuffer, 0);	// single edit buffers A-D
	}

	void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value)
	{
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource)
	{
		return false;
	}
}
