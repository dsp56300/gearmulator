#include "n2xController.h"

#include <fstream>

#include "BinaryData.h"
#include "n2xPatchManager.h"
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

	constexpr uint32_t g_multiPage = 10;
}

namespace n2xJucePlugin
{
	Controller::Controller(AudioPluginAudioProcessor& _p) : pluginLib::Controller(_p, loadParameterDescriptions()), m_state(nullptr)
	{
	    registerParams(_p);

		requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, 0);	// single edit buffers A-D
		requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, 1);
		requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, 2);
		requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, 3);

		requestDump(n2x::SysexByte::MultiRequestBankEditBuffer, 0);		// performance edit buffer

		requestDump(n2x::SysexByte::EmuGetPotsPosition, 0);

		m_currentPartChanged.set(onCurrentPartChanged, [this](const uint8_t& _part)
		{
			setMultiParameter(n2x::SelectedChannel, _part);
		});
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

	void Controller::onStateLoaded()
	{
		requestDump(n2x::SysexByte::EmuGetPotsPosition, 0);
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx& _msg, synthLib::MidiEventSource _source)
	{
		if(_msg.size() == n2x::g_singleDumpSize)
		{
			return parseSingleDump(_msg);
		}
		if(_msg.size() == n2x::g_multiDumpSize)
		{
			return parseMultiDump(_msg);
		}

		n2x::KnobType knobType;
		uint8_t knobValue;

		if(n2x::State::parseKnobSysex(knobType, knobValue, _msg))
		{
			if(m_state.receive(_msg, _source))
			{
				onKnobChanged(knobType, knobValue);
				return true;
			}
		}
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
			m_state.receive(_msg, synthLib::MidiEventSource::Plugin);
			applyPatchParameters(params, program);
			onProgramChanged();
			return true;
		}

		assert(false && "receiving a single for a non-edit-buffer is unexpected");
		return false;
	}

	bool Controller::parseMultiDump(const pluginLib::SysEx& _msg)
	{
		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::ParamValues params;

		if(!parseMidiPacket(midiPacketName(MidiPacketType::MultiDump), data, params, _msg))
			return false;

		const auto bank = data[pluginLib::MidiDataType::Bank];

		if(bank != n2x::SysexByte::MultiDumpBankEditBuffer)
			return false;

		m_state.receive(_msg, synthLib::MidiEventSource::Plugin);

		applyPatchParameters(params, 0);

		onProgramChanged();

		const auto part = m_state.getMultiParam(n2x::SelectedChannel, 0);
		if(part < getPartCount())	// if have seen dumps that have invalid stuff in here
		{
			// we ignore this for now, is annoying if the selected part changes whenever we load a multi
//			setCurrentPart(part);
		}

		return true;
	}

	bool Controller::parseControllerMessage(const synthLib::SMidiEvent& _e)
	{
		const auto& cm = getParameterDescriptions().getControllerMap();
		const auto paramIndices = cm.getControlledParameters(_e);

		if(paramIndices.empty())
			return false;

		const auto origin = midiEventSourceToParameterOrigin(_e.source);

		m_state.receive(_e);

		const auto parts = m_state.getPartsForMidiChannel(_e);

		for (const uint8_t part : parts)
		{
			if(_e.b == n2x::ControlChange::CCSync)
			{
				// this controls both Sync and RingMod
				// Sync = bit 0
				// RingMod = bit 1
				auto* paramSync = getParameter("Sync", part);
				auto* paramRingMod = getParameter("RingMod", part);
				paramSync->setValueFromSynth(_e.c & 1, origin);
				paramRingMod->setValueFromSynth((_e.c>>1) & 1, origin);
			}
			else
			{
				for (const auto paramIndex : paramIndices)
				{
					auto* param = getParameter(paramIndex, part);
					assert(param && "parameter not found for control change");
					param->setValueFromSynth(_e.c, origin);
				}
			}
		}

		return true;
	}

	void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value)
	{
		if(_parameter.getDescription().page >= g_multiPage)
		{
			sendMultiParameter(_parameter, static_cast<uint8_t>(_value));
			return;
		}

		constexpr uint32_t sysexRateLimitMs = 150;

		pluginLib::Parameter& nonConstParam = const_cast<pluginLib::Parameter&>(_parameter);

		const auto singleParam = static_cast<n2x::SingleParam>(_parameter.getDescription().index);
		const uint8_t part = _parameter.getPart();

		const auto& controllerMap = getParameterDescriptions().getControllerMap();

		uint32_t descIndex;
		if(!getParameterDescriptions().getIndexByName(descIndex, _parameter.getDescription().name))
			assert(false && "parameter not found");

		const auto& ccs = controllerMap.getControlChanges(synthLib::M_CONTROLCHANGE, descIndex);
		if(ccs.empty())
		{
			nonConstParam.setRateLimitMilliseconds(sysexRateLimitMs);
			setSingleParameter(part, singleParam, _value);
			return;
		}

		const auto cc = ccs.front();

		if(cc == n2x::ControlChange::CCSync)
		{
			// sync and ringmod have the same CC, combine them
			const auto* paramSync = getParameter("Sync", part);
			const auto* paramRingMod = getParameter("RingMod", part);

			_value = static_cast<uint8_t>(paramSync->getUnnormalizedValue() | (paramRingMod->getUnnormalizedValue() << 1));
		}

		const auto ch = m_state.getPartMidiChannel(part);

		const auto parts = m_state.getPartsForMidiChannel(ch);

		const auto ev = synthLib::SMidiEvent{synthLib::MidiEventSource::Editor, static_cast<uint8_t>(synthLib::M_CONTROLCHANGE + ch), cc, static_cast<uint8_t>(_value)};

		if(parts.size() > 1)
		{
			// this is problematic. We want to edit one part only but two parts receive on the same channel. We have to send a full dump
			nonConstParam.setRateLimitMilliseconds(sysexRateLimitMs);
			setSingleParameter(part, singleParam, static_cast<uint8_t>(_value));
		}
		else
		{
			nonConstParam.setRateLimitMilliseconds(0);
			m_state.receive(ev);
			sendMidiEvent(ev);
		}
	}

	void Controller::setSingleParameter(uint8_t _part, n2x::SingleParam _sp, uint8_t _value)
	{
		if(!m_state.changeSingleParameter(_part, _sp, _value))
			return;

		const auto& single = m_state.getSingle(_part);
		pluginLib::Controller::sendSysEx(pluginLib::SysEx{single.begin(), single.end()});
	}

	void Controller::setMultiParameter(n2x::MultiParam _mp, uint8_t _value)
	{
		if(!m_state.changeMultiParameter(_mp, _value))
			return;
		const auto& multi = m_state.updateAndGetMulti();
		pluginLib::Controller::sendSysEx(pluginLib::SysEx{multi.begin(), multi.end()});
	}

	uint8_t Controller::getMultiParameter(const n2x::MultiParam _param) const
	{
		return m_state.getMultiParam(_param, 0);
	}

	void Controller::sendMultiParameter(const pluginLib::Parameter& _parameter, const uint8_t _value)
	{
		const auto& desc = _parameter.getDescription();

		const auto mp = static_cast<n2x::MultiParam>(desc.index + (desc.page - g_multiPage) * 128);

		setMultiParameter(mp, _value);
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

	std::vector<uint8_t> Controller::createMultiDump(const n2x::SysexByte _bank, const uint8_t _program)
	{
		const auto multi = m_state.updateAndGetMulti();

		std::vector<uint8_t> result(multi.begin(), multi.end());

		result[n2x::SysexIndex::IdxMsgType] = _bank;
		result[n2x::SysexIndex::IdxMsgSpec] = _program;

		return result;
	}

	bool Controller::activatePatch(const std::vector<uint8_t>& _sysex, const uint32_t _part) const
	{
		if(_part >= getPartCount())
			return false;

		const auto isSingle =_sysex.size() == n2x::g_singleDumpSize;
		const auto isMulti = _sysex.size() == n2x::g_multiDumpSize;

		if(!isSingle && !isMulti)
			return false;

		auto d = _sysex;

		d[n2x::SysexIndex::IdxMsgType] = isSingle ? n2x::SysexByte::SingleDumpBankEditBuffer : n2x::SysexByte::MultiDumpBankEditBuffer;
		d[n2x::SysexIndex::IdxMsgSpec] = static_cast<uint8_t>(isMulti ? 0 : _part);

		pluginLib::Controller::sendSysEx(d);

		if(isSingle)
			requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, static_cast<uint8_t>(_part));
		else
		{
			requestDump(n2x::SysexByte::MultiRequestBankEditBuffer, 0);
			for(uint8_t i=0; i<getPartCount(); ++i)
				requestDump(n2x::SysexByte::SingleRequestBankEditBuffer, i);
		}

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

	std::string Controller::getSingleName(const uint8_t _part) const
	{
		const auto& single = m_state.getSingle(_part);
		return PatchManager::getPatchName({single.begin(), single.end()});
	}

	std::string Controller::getPatchName(const uint8_t _part) const
	{
		const auto& multi = m_state.getMulti();

		const auto bank = multi[n2x::SysexIndex::IdxMsgType];
		if(bank >= n2x::SysexByte::MultiDumpBankA)
			return PatchManager::getPatchName({multi.begin(), multi.end()});
		return getSingleName(_part);
	}

	bool Controller::getKnobState(uint8_t& _result, const n2x::KnobType _type) const
	{
		return m_state.getKnobState(_result, _type);
	}
}
