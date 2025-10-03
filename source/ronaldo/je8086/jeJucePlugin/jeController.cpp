#include "jeController.h"

#include "jePluginProcessor.h"

#include "dsp56kEmu/logging.h"

#include "jeLib/state.h"
#include "synthLib/midiToSysex.h"

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

			const auto index = desc.index | (desc.page * 0x100);

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

	bool Controller::sendSingle(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) const
	{
		// patches consist of multiple sysex messages, split them up again
		std::vector<std::vector<uint8_t>> sysex;
		synthLib::MidiToSysex::splitMultipleSysex(sysex, _patch->sysex);

		if (sysex.empty())
			return false;

		// is this a performance or a patch?
		const auto area = jeLib::State::getAddressArea(_patch->sysex);

		const auto isPerformance = area == jeLib::AddressArea::UserPerformance;
		const auto isPatch = area == jeLib::AddressArea::UserPatch;

		if (!isPerformance && !isPatch)
			return false;

		for (auto& s : sysex)
		{
			const auto addr = jeLib::State::getAddress(s);
			LOG(addr);

			if (isPatch)
			{
				const auto localAddr = static_cast<uint32_t>(addr) & static_cast<uint32_t>(jeLib::UserPatchArea::BlockMask);
				LOG(localAddr);

				const auto addr = 
					static_cast<uint32_t>(jeLib::AddressArea::PerformanceTemp) | 
					static_cast<uint32_t>(jeLib::PerformanceData::PatchUpper) | 
					localAddr;

				jeLib::State::setAddress(s, addr);
				jeLib::State::calcChecksum(s);

				sendSysEx(s);
			}
			else
			{
				const auto localAddr = static_cast<uint32_t>(addr) & static_cast<uint32_t>(jeLib::UserPerformanceArea::BlockMask);
				LOG(localAddr);

				const auto addr = 
					static_cast<uint32_t>(jeLib::AddressArea::PerformanceTemp) |
					localAddr;

				jeLib::State::setAddress(s, addr);
				jeLib::State::calcChecksum(s);

				sendSysEx(s);
			}
		}

		return true;
	}
}
