#include "jeController.h"

#include "jePluginProcessor.h"

#include "dsp56kEmu/logging.h"

#include "jeLib/state.h"
#include "synthLib/midiToSysex.h"

namespace jeJucePlugin
{
	namespace
	{
		constexpr uint8_t g_paramPagePatch = 0;
		constexpr uint8_t g_paramPagePart = 2;
		constexpr uint8_t g_paramPagePerformance = 3;

		constexpr size_t g_dataStartIndex = std::size(jeLib::g_sysexHeader) + 1/*command*/ + std::tuple_size_v<rLib::Storage::Address4>;
	}

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

		sendSystemRequest();
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

		if (desc.page == g_paramPagePerformance)
		{
			// performance parameter
			const auto msg = jeLib::State::createParameterChange(static_cast<jeLib::PerformanceCommon>(desc.index), _value);
			sendSysEx(msg);
		}
		else if (desc.page == g_paramPagePart)
		{
			// part parameter
			const auto lowerUpper = getCurrentPart() > 0 ? jeLib::PerformanceData::PartLower : jeLib::PerformanceData::PartUpper;

			const auto msg = jeLib::State::createParameterChange(lowerUpper, static_cast<jeLib::Part>(desc.index), _value);

			sendSysEx(msg);
		}
		else if (desc.page == g_paramPagePatch || desc.page == g_paramPagePatch + 1)
		{
			// patch parameter
			const auto index = desc.index | (desc.page * 0x100);

			const auto lowerUpper = getCurrentPart() > 0 ? jeLib::PerformanceData::PatchLower : jeLib::PerformanceData::PatchUpper;

			const auto msg = jeLib::State::createParameterChange(lowerUpper, static_cast<jeLib::Patch>(index), _value);

			sendSysEx(msg);
		}
		else
		{
			assert(false && "unsupported parameter page");
		}
	}

	void Controller::sendParameterChange(const jeLib::SystemParameter _parameter, const int _value) const
	{
		const auto msg = jeLib::State::createParameterChange(_parameter, _value);
		sendSysEx(msg);
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
				case jeLib::PerformanceData::PerformanceCommon:
					parsePerformanceCommon(_sysex);
					break;
				case jeLib::PerformanceData::PatchUpper:
				case jeLib::PerformanceData::PatchLower:
					parsePatch(_sysex, perfData == jeLib::PerformanceData::PatchUpper ? 0 : 1);
					break;
				case jeLib::PerformanceData::PartUpper:
				case jeLib::PerformanceData::PartLower:
					parsePart(_sysex, perfData == jeLib::PerformanceData::PartUpper ? 0 : 1);
					break;
				default:;
				}
			}
			break;
		case jeLib::AddressArea::UserPatch:
			{
			}
			break;
		case jeLib::AddressArea::System:
			{
				auto systemArea = static_cast<jeLib::SystemArea>(addr & 0xffff);

				switch (systemArea)
				{
				case jeLib::SystemArea::SystemParameter:
					parseSystemParameters(_sysex);
					break;
				case jeLib::SystemArea::PatternSetup:
					break;
				case jeLib::SystemArea::MotionSetup:
					break;
				case jeLib::SystemArea::TxRxSetting:
					break;
				}
			}
			break;
		default:
			break;
		}

		return false;
	}

	void Controller::parsePerformanceCommon(const pluginLib::SysEx& _sysex) const
	{
		const auto address = jeLib::State::getAddress(_sysex);

		uint32_t addr = address & 0xff;

		for (size_t i=g_dataStartIndex; i<_sysex.size() - std::size(jeLib::g_sysexFooter); ++i, ++addr)
		{
			const auto parameterType = static_cast<jeLib::PerformanceCommon>(addr);

			const auto parameterIndex = getParameterDescriptions().getAbsoluteIndex(g_paramPagePerformance, static_cast<uint8_t>(addr));

			auto* param = getParameter(parameterIndex, 0);
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

			param->setValueFromSynth(value, pluginLib::Parameter::Origin::PresetChange);
		}
	}

	void Controller::parsePatch(const pluginLib::SysEx& _sysex, const uint8_t _part) const
	{
		const auto address = jeLib::State::getAddress(_sysex);

		uint32_t addr = address & static_cast<uint32_t>(jeLib::UserPatchArea::BlockMask);

		for (size_t i=g_dataStartIndex; i<_sysex.size() - std::size(jeLib::g_sysexFooter); ++i, ++addr)
		{
			rLib::Storage::Address4 addr4;

			addr4[2] = static_cast<uint8_t>((addr >> 7) & 0x7F);
			addr4[3] = static_cast<uint8_t>(addr & 0x7F);

			const auto parameterType = static_cast<jeLib::Patch>((addr4[2] << 8) + addr4[3]);

			const auto parameterIndex = getParameterDescriptions().getAbsoluteIndex(addr4[2] + g_paramPagePatch, addr4[3]);

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

			param->setValueFromSynth(value, pluginLib::Parameter::Origin::PresetChange);
		}
	}

	void Controller::parsePart(const pluginLib::SysEx& _sysex, const uint8_t _part) const
	{
		const auto address = jeLib::State::getAddress(_sysex);

		uint32_t addr = address & 0xff;

		for (size_t i=g_dataStartIndex; i<_sysex.size() - std::size(jeLib::g_sysexFooter); ++i, ++addr)
		{
			const auto parameterIndex = getParameterDescriptions().getAbsoluteIndex(g_paramPagePart, static_cast<uint8_t>(addr));

			auto* param = getParameter(parameterIndex, _part);
			assert(param && "parameter not found");

			if(!param)
				break;

			pluginLib::ParamValue value = _sysex[i];

			param->setValueFromSynth(value, pluginLib::Parameter::Origin::PresetChange);
		}
	}

	void Controller::parseSystemParameters(const pluginLib::SysEx& _sysex)
	{
		// upon receiving the system parameters, we adjust them to our needs if they don't match

		const auto address = jeLib::State::getAddress(_sysex);
		uint32_t addr = address & 0xff;

		for (size_t i=g_dataStartIndex; i<_sysex.size() - std::size(jeLib::g_sysexFooter); ++i, ++addr)
		{
			const auto parameterType = static_cast<jeLib::SystemParameter>(addr);
			const auto value = _sysex[i];

			auto sendChange = [&](const uint8_t _expectedValue)
			{
				if (value == _expectedValue)
					return;
				sendParameterChange(parameterType, _expectedValue);
			};

			switch (parameterType)
			{
			case jeLib::SystemParameter::MidiSync:				// Enable sync to external MIDI clock
				sendChange(1);
				break;
			case jeLib::SystemParameter::RemoteControlChannel:	// Set remote channel to 3
				sendChange(2);
				break;
			case jeLib::SystemParameter::MasterTune:
				sendChange(32);	// 440Hz
			case jeLib::PerformanceControlChannel:
				sendChange(0x11);	// off
				break;
			case jeLib::LocalSwitch:
				sendChange(1);
				break;
			case jeLib::TxRxEditMode:
				sendChange(0); // MODE1
				break;
			case jeLib::TxRxEditSwitch:
				break;
			case jeLib::TxRxProgramChangeSwitch:
				sendChange(0);	// disable program changes, we handle them ourselves
				break;
			case jeLib::KeyboardShift:
				sendChange(2); // = 0
				break;
			case jeLib::RemoteKeyboardChannel:
				sendChange(4); // only relevant for the rack
				break;
			default:;
			}
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

				const auto a = 
					static_cast<uint32_t>(jeLib::AddressArea::PerformanceTemp) | 
					static_cast<uint32_t>(jeLib::PerformanceData::PatchUpper) | 
					localAddr;

				jeLib::State::setAddress(s, a);
				jeLib::State::calcChecksum(s);

				sendSysEx(s);
			}
			else
			{
				const auto localAddr = static_cast<uint32_t>(addr) & static_cast<uint32_t>(jeLib::UserPerformanceArea::BlockMask);
				LOG(localAddr);

				const auto a = 
					static_cast<uint32_t>(jeLib::AddressArea::PerformanceTemp) |
					localAddr;

				jeLib::State::setAddress(s, a);
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

	void Controller::sendSystemRequest() const
	{
		sendSysEx(jeLib::State::createSystemRequest());
	}
}
