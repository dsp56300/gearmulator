#include "jeController.h"

#include "jePluginProcessor.h"

#include "dsp56kEmu/logging.h"

#include "jeLib/state.h"

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
		const auto& desc = _parameter.getDescription();

		if (desc.page < 2)
		{
			// patch parameter

			const auto index = desc.index + (desc.page * 128);

			const auto lowerUpper = getCurrentPart() > 0 ? jeLib::PerformanceData::PatchLower : jeLib::PerformanceData::PatchUpper;

			const auto msg = jeLib::State::createParameterChange(lowerUpper, static_cast<jeLib::Patch>(index), _value);

			LOG("Send paramchange: " << _parameter.getDescription().name << " (" << index << ") = " << _value);

			sendSysEx(msg);
		}
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource)
	{
		return false;
	}
}
