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

		sendTempPerformanceRequest();
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

			sendSysEx(msg);
		}
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx& _sysex, synthLib::MidiEventSource _source)
	{
		if (m_sysexRemote.receive(_sysex))
			return true;

		const auto addr = jeLib::State::getAddress(_sysex);
		const auto area = jeLib::State::getAddressArea(addr);

		switch (area)
		{
		case jeLib::AddressArea::PerformanceTemp:
		case jeLib::AddressArea::UserPerformance:
			{
				const auto perfData = static_cast<jeLib::PerformanceData>(addr & 0xffff);

				switch (perfData)
				{
				case jeLib::PerformanceData::PatchUpper:
				case jeLib::PerformanceData::PatchLower:
					parsePatch(_sysex, _source, perfData == jeLib::PatchUpper ? 0 : 1);
					break;
				default:;
				}
			}
			break;
		case jeLib::AddressArea::UserPatch:
			{
			}
			break;
		default:
			break;
		}

		return false;
	}
	
	void Controller::parsePatch(const pluginLib::SysEx& _sysex, synthLib::MidiEventSource _source, uint8_t _part)
	{
		const auto address = jeLib::State::getAddress(_sysex);

		uint32_t addr = address & static_cast<uint32_t>(jeLib::UserPatchArea::BlockMask);

		constexpr size_t startIndex = std::size(jeLib::g_sysexHeader) + 1/*command*/ + std::tuple_size_v<rLib::Storage::Address4>;

		for (size_t i=startIndex; i<_sysex.size() - std::size(jeLib::g_sysexFooter); ++i, ++addr)
		{
			rLib::Storage::Address4 addr4;

			addr4[2] = static_cast<uint8_t>((addr >> 7) & 0x7F);
			addr4[3] = static_cast<uint8_t>(addr & 0x7F);

			const auto parameterType = static_cast<jeLib::Patch>((addr4[2] << 8) + addr4[3]);

			const auto parameterIndex = getParameterDescriptions().getAbsoluteIndex(addr4[2], addr4[3]);

			auto* param = getParameter(parameterIndex, _part);
			assert(param && "parameter not found");

			if(!param)
				break;

			pluginLib::ParamValue value;

			if(jeLib::State::is14BitData(parameterType))
			{
				if(i + 1 >= _sysex.size() - std::size(jeLib::g_sysexFooter))
					break;
				value = (_sysex[i] << 7) | _sysex[i + 1];
				++i;
				++addr;
			}
			else
			{
				value = _sysex[i];
			}

			if (parameterType == jeLib::Patch::Osc1Waveform)
				int foo=0;
			param->setValueFromSynth(value, pluginLib::Parameter::Origin::PresetChange);
		}
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

	void Controller::sendTempPerformanceRequest() const
	{
		sendPerformanceRequest(jeLib::AddressArea::PerformanceTemp, jeLib::UserPerformanceArea::UserPerformance01);
	}

	void Controller::sendPerformanceRequest(const jeLib::AddressArea _area, const jeLib::UserPerformanceArea _performance) const
	{
		sendSysEx(jeLib::State::createPerformanceRequest(_area, _performance));
	}
}
