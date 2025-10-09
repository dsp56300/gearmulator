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
	}

	Controller::~Controller() = default;

	void Controller::onStateLoaded()
	{
		sendSystemRequest();
		sendTempPerformanceRequest();
	}

	void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value, const pluginLib::Parameter::Origin _origin)
	{
		const auto& desc = _parameter.getDescription();

		if (desc.page == g_paramPagePerformance)
		{
			// performance parameter
			const auto msg = jeLib::State::createParameterChange(static_cast<jeLib::PerformanceCommon>(desc.index), _value);
			sendSysEx(msg);
			m_state.receive(msg);
		}
		else if (desc.page == g_paramPagePart)
		{
			// part parameter
			const auto lowerUpper = _parameter.getPart() > 0 ? jeLib::PerformanceData::PartLower : jeLib::PerformanceData::PartUpper;

			const auto msg = jeLib::State::createParameterChange(lowerUpper, static_cast<jeLib::Part>(desc.index), _value);

			sendSysEx(msg);
			m_state.receive(msg);
		}
		else if (desc.page == g_paramPagePatch || desc.page == g_paramPagePatch + 1)
		{
			// patch parameter
			const auto index = desc.index | (desc.page * 0x100);

			const auto lowerUpper = _parameter.getPart() > 0 ? jeLib::PerformanceData::PatchLower : jeLib::PerformanceData::PatchUpper;

			const auto msg = jeLib::State::createParameterChange(lowerUpper, static_cast<jeLib::Patch>(index), _value);

			sendSysEx(msg);
			m_state.receive(msg);
		}
		else
		{
			assert(false && "unsupported parameter page");
			return;
		}

		// if both parts are selected, apply the change to the other part as well
		if (desc.page != g_paramPagePerformance && isBothSelected() && _parameter.getPart() == getCurrentPart())
		{
			if (_origin == pluginLib::Parameter::Origin::Ui || _origin == pluginLib::Parameter::Origin::Midi || _origin == pluginLib::Parameter::Origin::HostAutomation)
			{
				auto otherPart = static_cast<uint8_t>((_parameter.getPart() + 1) & 1);
				auto* p = getParameter(_parameter.getDescription().name, otherPart);
				assert(p);
				p->setUnnormalizedValueNotifyingHost(_parameter.getUnnormalizedValue(), pluginLib::Parameter::Origin::Ui);
			}
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

		m_state.receive(_sysex);

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
				case jeLib::SystemArea::MotionSetup:
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

	bool Controller::isUpperSelected() const
	{
		return (getParameter("PanelSelect", 0)->getUnnormalizedValue() + 1) & 1;
	}

	bool Controller::isLowerSelected() const
	{
		return (getParameter("PanelSelect", 0)->getUnnormalizedValue() + 1) & 2;
	}

	bool Controller::isBothSelected() const
	{
		return getParameter("PanelSelect", 0)->getUnnormalizedValue() == 2;
	}

	bool Controller::requestPatchForPart(std::vector<uint8_t>& _data, uint32_t _part, uint64_t _userData) const
	{
		std::vector<synthLib::SMidiEvent> results;

		const auto isPerformance = _userData == 2;

		if (_userData == 2)
		{
			m_state.createTempPerformanceDumps(results);
		}
		else
		{
			switch (_part)
			{
			case 0:  m_state.createTempPerformanceDumps(results, jeLib::PerformanceData::PatchUpper); break;
			case 1:  m_state.createTempPerformanceDumps(results, jeLib::PerformanceData::PatchLower); break;
			default: return false;
			}
		}

		for (auto& result : results)
		{
			// make sure the address is correct, we do not want to return temp performance but something stored in user memory
			if (isPerformance)
				jeLib::State::setAddress(result.sysex, static_cast<uint32_t>(jeLib::AddressArea::UserPerformance));
			else
				jeLib::State::setAddress(result.sysex, static_cast<uint32_t>(jeLib::AddressArea::UserPatch));

			_data.insert(_data.end(), result.sysex.begin(), result.sysex.end());
		}

		return true;
	}

	bool Controller::changePatchName(PatchType _type, const std::string& _newName) const
	{
		if (getPatchName(_type) == _newName)
			return false;

		if (_type == PatchType::Performance)
		{
			std::vector<synthLib::SMidiEvent> dumps;

			if (!m_state.createTempPerformanceDumps(dumps, jeLib::PerformanceData::PerformanceCommon))
				return false;

			// we only need the first dump as its the only one containing the name
			auto& sysex = dumps.front();
			if (!jeLib::State::setName(sysex.sysex, _newName))
				return false;
			jeLib::State::updateChecksum(sysex.sysex);
			sendSysEx(sysex.sysex);
			sendTempPerformanceRequest();
			return true;
		}

		const auto part = _type == PatchType::PartUpper ? 0 : 1;

		std::vector<uint8_t> data;
		if (!requestPatchForPart(data, part, part))
			return false;

		if (!jeLib::State::setName(data, _newName))
			return false;

		return sendSingle(data, part);
	}

	void Controller::parsePerformanceCommon(const pluginLib::SysEx& _sysex)
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

		if (auto name = jeLib::State::getName(_sysex))
			setPatchName(PatchType::Performance, *name);
	}

	void Controller::parsePatch(const pluginLib::SysEx& _sysex, const uint8_t _part)
	{
		const auto address = jeLib::State::getAddress(_sysex);

		uint32_t addr = address & static_cast<uint32_t>(jeLib::UserPatchArea::BlockMask);

		// might be a partial patch, only set the name if we are at the start
		if (addr == 0)
		{
			if (const auto name = jeLib::State::getName(_sysex))
				setPatchName(_part == 0 ? PatchType::PartUpper : PatchType::PartLower, *name);
		}

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

	void Controller::parseSystemParameters(const pluginLib::SysEx& _sysex) const
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
				sendChange(50);	// 440Hz
				break;
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

	bool Controller::sendSingle(const pluginLib::SysEx& _sysex, uint32_t _part) const
	{
		// patches consist of multiple sysex messages, split them up again
		std::vector<std::vector<uint8_t>> sysex;
		synthLib::MidiToSysex::splitMultipleSysex(sysex, _sysex);

		if (sysex.empty())
			return false;

		// is this a performance or a patch?
		const auto area = jeLib::State::getAddressArea(sysex.front());

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
				auto sendPatch = [&](jeLib::PerformanceData _upperLower)
				{
					const auto localAddr = static_cast<uint32_t>(addr) & static_cast<uint32_t>(jeLib::UserPatchArea::BlockMask);
					LOG(localAddr);

					const auto a = 
						static_cast<uint32_t>(jeLib::AddressArea::PerformanceTemp) | 
						static_cast<uint32_t>(_upperLower) | 
						localAddr;

					jeLib::State::setAddress(s, a);
					jeLib::State::updateChecksum(s);

					sendSysEx(s);
				};

				sendPatch(_part == 0 ? jeLib::PerformanceData::PatchUpper : jeLib::PerformanceData::PatchLower);
			}
			else
			{
				const auto localAddr = static_cast<uint32_t>(addr) & static_cast<uint32_t>(jeLib::PerformanceData::BlockMask);
				LOG(localAddr);

				const auto a = 
					static_cast<uint32_t>(jeLib::AddressArea::PerformanceTemp) |
					localAddr;

				jeLib::State::setAddress(s, a);
				jeLib::State::updateChecksum(s);

				sendSysEx(s);
			}
		}

		sendTempPerformanceRequest();

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

	void Controller::setPatchName(PatchType _type, const std::string& _name)
	{
		auto& currentName = m_patchNames[static_cast<size_t>(_type)];

		if (currentName == _name)
			return;

		currentName = _name;

		evPatchNameChanged(_type, _name);
	}
}
