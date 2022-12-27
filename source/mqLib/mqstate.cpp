#include "mqstate.h"

#include <cassert>

#include "mqmiditypes.h"
#include "microq.h"

#include "../synthLib/os.h"
#include "../synthLib/midiToSysex.h"
#include "../synthLib/midiBufferParser.h"

#pragma optimize("", off)

namespace mqLib
{
	static_assert(std::size(State::g_dumps) == static_cast<uint32_t>(State::DumpType::Count), "data definition missing");

	State::State(MicroQ& _mq) : m_mq(_mq)
	{
	}

	bool State::loadState(const SysEx& _sysex)
	{
		std::vector<std::vector<uint8_t>> messages;
		synthLib::MidiToSysex::splitMultipleSysex(messages, _sysex);

		if(messages.empty())
			return false;

		for (const auto& message : messages)
		{
			Responses nop;

			if(message.size() == 207)
				continue;

			if(!receive(nop, message, Origin::External))
				continue;

			if(message[IdxBuffer] == static_cast<uint8_t>(MidiBufferNum::SingleEditBufferSingleMode))
			{
				auto m = message;
				m[IdxBuffer] = static_cast<uint8_t>(MidiBufferNum::DeprecatedSingleBankA);
				for(uint8_t i=0; i<10; ++i)
				{
					m[IdxLocation] = i;
					updateChecksum(m);
					forwardToDevice(m);
				}
			}
			else
			{
				forwardToDevice(message);
			}
		}
		return true;
	}

	bool State::receive(Responses& _responses, const SysEx& _data, Origin _sender)
	{
		if(_data.size() < 5)
			return false;

		if(_data.front() != 0xf0 || _data.back() != 0xf7)
			return false;

		if(_data[IdxIdWaldorf] != IdWaldorf || _data[IdxIdMicroQ] != IdMicroQ)
			return false;

		m_sender = _sender;
		m_isEditBuffer = false;

		const auto cmd = static_cast<SysexCommand>(_data[IdxCommand]);

		switch (cmd)
		{
		case SysexCommand::SingleRequest:			return getDump(DumpType::Single, _responses, _data);
		case SysexCommand::MultiRequest:			return getDump(DumpType::Multi,_responses, _data);
		case SysexCommand::DrumRequest:				return getDump(DumpType::Drum, _responses, _data);
		case SysexCommand::GlobalRequest:			return getDump(DumpType::Global, _responses, _data);
		case SysexCommand::ModeRequest:				return getDump(DumpType::Mode, _responses, _data);

		case SysexCommand::SingleDump:				return parseDump(DumpType::Single, _data);
		case SysexCommand::MultiDump:				return parseDump(DumpType::Multi, _data);
		case SysexCommand::DrumDump:				return parseDump(DumpType::Drum, _data);
		case SysexCommand::GlobalDump:				return parseDump(DumpType::Global, _data);
		case SysexCommand::ModeDump:				return parseDump(DumpType::Mode, _data);

		case SysexCommand::SingleParameterChange:	return modifyDump(DumpType::Single, _data);
		case SysexCommand::MultiParameterChange:	return modifyDump(DumpType::Multi, _data);
		case SysexCommand::DrumParameterChange:		return modifyDump(DumpType::Drum, _data);
		case SysexCommand::GlobalParameterChange:	return modifyDump(DumpType::Global, _data);
		case SysexCommand::ModeParameterChange:		return modifyDump(DumpType::Mode, _data);

		case SysexCommand::SingleParameterRequest:	return requestDumpParameter(DumpType::Single, _responses, _data);
		case SysexCommand::MultiParameterRequest:	return requestDumpParameter(DumpType::Multi, _responses, _data);
		case SysexCommand::DrumParameterRequest:	return requestDumpParameter(DumpType::Drum, _responses, _data);
		case SysexCommand::GlobalParameterRequest:	return requestDumpParameter(DumpType::Global, _responses, _data);
		case SysexCommand::ModeParameterRequest:	return requestDumpParameter(DumpType::Mode, _responses, _data);

/*		case SysexCommand::EmuLCD:
		case SysexCommand::EmuLEDs:
		case SysexCommand::EmuButtons:
		case SysexCommand::EmuRotaries:
			return false;
*/		default:
			return false;
		}
	}

	void State::createInitState()
	{
		// request global settings and wait for them. Once they are valid, send init state
		m_mq.sendMidi({0xf0, IdWaldorf, IdMicroQ, IdDeviceOmni, static_cast<uint8_t>(SysexCommand::GlobalRequest), 0xf7});
//		m_mq.sendMidi({0xf0, IdWaldorf, IdMicroQ, IdDeviceOmni, static_cast<uint8_t>(SysexCommand::ModeRequest), 0xf7});

		synthLib::MidiBufferParser parser;
		Responses unused;
		std::vector<uint8_t> midi;
		std::vector<synthLib::SMidiEvent> events;

		while(!isValid(m_global))// || !isValid(m_mode))
		{
			m_mq.process(8);
			midi.clear();
			m_mq.receiveMidi(midi);
			parser.write(midi);

			events.clear();
			parser.getEvents(events);

			for (const auto & event : events)
			{
				if(!event.sysex.empty())
				{
					if(!receive(unused, event.sysex, Origin::Device))
						assert(false);
				}
			}
		}

		auto setParam = [&](GlobalParameter _param, uint8_t _value)
		{
			setGlobalParameter(_param, _value);
			return;
			const auto parameterIndex = static_cast<uint32_t>(_param);
			const auto pah = static_cast<uint8_t>(parameterIndex >> 7);
			const auto pal = static_cast<uint8_t>(parameterIndex & 0x7f);
			const SysEx msg({0xf0, IdWaldorf, IdMicroQ, IdDeviceOmni, static_cast<uint8_t>(SysexCommand::GlobalParameterChange), pah, pal, _value, 0xf7});
			
			receive(unused, msg, Origin::External);
		};

		setParam(GlobalParameter::InstrumentSelection, 0);		// Select first instrument
		setParam(GlobalParameter::MultiNumber, 0);				// First Multi

		setParam(GlobalParameter::InstrumentABankNumber, 0);	// First bank for all, singles 0-3
		setParam(GlobalParameter::InstrumentASingleNumber, 0);	// 
		setParam(GlobalParameter::InstrumentBBankNumber, 0);	// 
		setParam(GlobalParameter::InstrumentBSingleNumber, 1);	// 
		setParam(GlobalParameter::InstrumentCBankNumber, 0);	// 
		setParam(GlobalParameter::InstrumentCSingleNumber, 2);	// 
		setParam(GlobalParameter::InstrumentDBankNumber, 0);	// 
		setParam(GlobalParameter::InstrumentDSingleNumber, 3);	// 

		setParam(GlobalParameter::Tuning, 64);					// 440 Hz
		setParam(GlobalParameter::Transpose, 64);				// +/- 0
		setParam(GlobalParameter::ControllerSend, 2);			// SysEx
		setParam(GlobalParameter::ControllerReceive, 1);		// On
		setParam(GlobalParameter::ArpSend, 0);					// Off
		setParam(GlobalParameter::Clock, 2);					// Auto
		setParam(GlobalParameter::MidiChannel, 0);				// omni
		setParam(GlobalParameter::SysExDeviceId, 0);			// 0
		setParam(GlobalParameter::LocalControl, 1);				// On
		setParam(GlobalParameter::ProgramChangeRx, 2);			// Number + Bank
		setParam(GlobalParameter::ProgramChangeTx, 2);			// Number + Bank

		setParam(GlobalParameter::SingleMultiMode, 0);			// Single mode

		receive(unused, convertTo(m_global), Origin::External);

		/*
		// accept files up to 300k as larger files might be the OS
		const auto midifile = synthLib::findFile(".mid", 0, 300 * 1024);
		if(!midifile.empty())
		{
			std::vector<uint8_t> sysex;
			synthLib::MidiToSysex::readFile(sysex, midifile.c_str());
			if(!sysex.empty())
				loadState(sysex);
		}

		if(isValid(m_romSingles[0]))
		{
			auto dump = convertTo(m_romSingles[0]);

			dump[IdxBuffer]   = static_cast<uint8_t>(MidiBufferNum::SingleEditBufferSingleMode);
			dump[IdxLocation] = 0;

			forwardToDevice(dump);
		}
		*/
	}

	bool State::getState(std::vector<uint8_t>& _state, synthLib::StateType _type) const
	{
		append(_state, m_global);
		append(_state, m_currentDrumMap);
		append(_state, m_currentMulti);

		for (const auto& s : m_currentMultiSingles)
			append(_state, s);

		for (const auto& s : m_currentDrumMapSingles)
			append(_state, s);

		for (const auto& s : m_currentInstrumentSingles)
			append(_state, s);

		return !_state.empty();
	}

	bool State::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return loadState(_state);
	}

	bool State::parseSingleDump(const SysEx& _data)
	{
		Single single;

		if(!convertTo(single, _data))
			return false;

		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		Single* dst = getSingle(buf, loc);

		if(!dst)
			return false;
		*dst = single;
		return true;
	}

	bool State::parseMultiDump(const SysEx& _data)
	{
		Multi multi;

		if(!convertTo(multi, _data))
			return false;

		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		auto* m = getMulti(buf, loc);
		if(!m)
			return false;
		*m = multi;
		return true;
	}

	bool State::parseDrumDump(const SysEx& _data)
	{
		DrumMap drum;

		if(!convertTo(drum, _data))
			return false;

		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		auto* d = getDrumMap(buf, loc);
		if(!d)
			return false;
		*d = drum;
		return true;
	}

	bool State::parseGlobalDump(const SysEx& _data)
	{
		return convertTo(m_global, _data);
	}

	bool State::parseModeDump(const SysEx& _data)
	{
		return convertTo(m_mode, _data);
	}

	bool State::modifySingle(const SysEx& _data)
	{
		auto* p = getSingleParameter(_data);
		if(!p)
			return false;
		*p = _data[IdxSingleParamValue];
		return true;
	}

	bool State::modifyMulti(const SysEx& _data)
	{
		auto* p = getMultiParameter(_data);
		if(!p)
			return false;

		*p = _data[IdxMultiParamValue];
		return true;
	}

	bool State::modifyDrum(const SysEx& _data)
	{
		auto* p = getDrumParameter(_data);
		if(!p)
			return false;

		*p = _data[IdxDrumParamValue];
		return true;
	}

	bool State::modifyGlobal(const SysEx& _data)
	{
		auto* p = getGlobalParameter(_data);
		if(!p)
			return false;

		*p = _data[IdxGlobalParamValue];
		return true;
	}

	bool State::modifyMode(const SysEx& _data)
	{
		auto* p = getModeParameter(_data);
		if(!p)
			return false;

		*p = _data[IdxModeParamValue];
		return true;
	}
	
	template<size_t Size>
	static uint8_t* getParameter(std::array<uint8_t, Size>& _dump, const SysEx& _data, State::DumpType _type)
	{
		const auto& dump = State::g_dumps[static_cast<uint32_t>(_type)];

		if(dump.idxParamIndexH >= _data.size() || dump.idxParamIndexL >= _data.size())
			return nullptr;

		const auto i = dump.firstParamIndex + ((static_cast<uint32_t>(_data[dump.idxParamIndexH]) << 7) | static_cast<uint32_t>(_data[dump.idxParamIndexL]));

		if(i > _dump.size())
			return nullptr;
		return &_dump[i];
	}

	uint8_t* State::getSingleParameter(const SysEx& _data)
	{
		const auto loc = _data[IdxBuffer];

		Single* s = getSingle(getGlobalParameter(GlobalParameter::SingleMultiMode) ? MidiBufferNum::SingleEditBufferSingleMode : MidiBufferNum::SingleEditBufferMultiMode, loc);
		if(!s)
			return nullptr;
		return getParameter(*s, _data, DumpType::Single);
	}

	uint8_t* State::getMultiParameter(const SysEx& _data)
	{
		return getParameter(m_currentMulti, _data, DumpType::Multi);
	}

	uint8_t* State::getDrumParameter(const SysEx& _data)
	{
		return getParameter(m_currentDrumMap, _data, DumpType::Drum);
	}

	uint8_t* State::getGlobalParameter(const SysEx& _data)
	{
		return getParameter(m_global, _data, DumpType::Global);
	}

	uint8_t* State::getModeParameter(const SysEx& _data)
	{
		return getParameter(m_mode, _data, DumpType::Mode);
	}

	bool State::getSingle(Responses& _responses, const SysEx& _data)
	{
		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		const auto* s = getSingle(buf, loc);
		if(!s || !isValid(*s))
			return false;
		_responses.push_back(convertTo(*s));
		return true;
	}

	State::Single* State::getSingle(MidiBufferNum _buf, uint8_t _loc)
	{
		switch (_buf)
		{
		case MidiBufferNum::SingleBankA:
		case MidiBufferNum::DeprecatedSingleBankA:
			if(_loc >= 100)
				return nullptr;
			return &m_romSingles[_loc];
		case MidiBufferNum::SingleBankB:
		case MidiBufferNum::DeprecatedSingleBankB:
			if(_loc >= 100)
				return nullptr;
			return &m_romSingles[_loc + 100];
		case MidiBufferNum::SingleBankC:
		case MidiBufferNum::DeprecatedSingleBankC:
			if(_loc >= 100)
				return nullptr;
			return &m_romSingles[_loc + 200];
		case MidiBufferNum::SingleBankX:
		case MidiBufferNum::DeprecatedSingleBankX:
			// mQ doesn't have a card, no idea why its mentioned in the MIDI implementaiton
			return nullptr;
		case MidiBufferNum::SingleEditBufferSingleMode:
			m_isEditBuffer = true;
			return &m_currentInstrumentSingles[getGlobalParameter(GlobalParameter::InstrumentSelection)];
		case MidiBufferNum::SingleEditBufferMultiMode:
//		case MidiBufferNum::EditBufferSingleLayer: (same value as above)
			{
				m_isEditBuffer = true;
				if(_loc >= 0x10 && _loc <= 0x2f)
					return &m_currentDrumMapSingles[_loc - 0x10];
				if(getGlobalParameter(GlobalParameter::SingleMultiMode) > 0)
				{
					if(_loc >= m_currentMultiSingles.size())
						return nullptr;
					return &m_currentMultiSingles[_loc];
				}
				if(_loc >= m_currentInstrumentSingles.size())
					return nullptr;
				return &m_currentInstrumentSingles[_loc];
			}
		default:
			return nullptr;
		}
	}

	bool State::getMulti(Responses& _responses, const SysEx& _data)
	{
		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		auto* m = getMulti(buf, loc);
		if(!m || !isValid(*m))
			return false;
		_responses.push_back(convertTo(*m));
		return true;
	}

	State::Multi* State::getMulti(MidiBufferNum buf, uint8_t loc)
	{
		switch (buf)
		{
		case MidiBufferNum::MultiEditBuffer:
			m_isEditBuffer = true;
			return &m_currentMulti;
		case MidiBufferNum::DeprecatedMultiBankInternal:
		case MidiBufferNum::MultiBankInternal:
			if(loc >= 100)
				return nullptr;
			return &m_romMultis[loc];
/*		case MidiBufferNum::DeprecatedMultiBankCard:
		case MidiBufferNum::MultiBankCard:
			// mQ doesn't have a card even though MIDI doc mentions it
			return nullptr;
*/		default:
			return nullptr;
		}
	}

	bool State::getDrumMap(Responses& _responses, const SysEx& _data)
	{
		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		auto* d = getDrumMap(buf, loc);
		if(!d || !isValid(*d))
			return false;
		_responses.push_back(convertTo(*d));
		return true;
	}

	State::DrumMap* State::getDrumMap(const MidiBufferNum _buf, const uint8_t _loc)
	{
		switch (_buf)
		{
		case MidiBufferNum::DeprecatedDrumBankInternal:
		case MidiBufferNum::DrumBankInternal:
			if(_loc >= 20)
				return nullptr;
			return &m_romDrumMaps[_loc];
		case MidiBufferNum::DeprecatedDrumBankCard:
		case MidiBufferNum::DrumBankCard:
			return nullptr;
		case MidiBufferNum::DrumEditBuffer:
			m_isEditBuffer = true;
			return &m_currentDrumMap;
		default:
			return nullptr;
		}
	}

	bool State::getGlobal(Responses& _responses)
	{
		const auto* g = getGlobal();
		if(g == nullptr)
			return false;
		_responses.push_back(convertTo(*g));
		return true;
	}

	State::Global* State::getGlobal()
	{
		if(isValid(m_global))
		{
			m_isEditBuffer = true;
			return &m_global;
		}
		return nullptr;
	}

	bool State::getMode(Responses& _responses)
	{
		const auto* m = getMode();
		if(m == nullptr)
			return false;
		_responses.push_back(convertTo(*m));
		return true;
	}

	State::Mode* State::getMode()
	{
		if(isValid(m_mode))
		{
			m_isEditBuffer = true;
			return &m_mode;
		}
		return nullptr;
	}

	bool State::getDump(DumpType _type, Responses& _responses, const SysEx& _data)
	{
		bool res;

		switch (_type)
		{
		case DumpType::Single: res = getSingle(_responses, _data); break;
		case DumpType::Multi: res = getMulti(_responses, _data); break;
		case DumpType::Drum: res = getDrumMap(_responses, _data); break;
		case DumpType::Global: res = getGlobal(_responses); break;
		case DumpType::Mode: res = getMode(_responses); break;
		default:
			return false;
		}

		if(!res)
			forwardToDevice(_data);
		return res;
	}

	bool State::parseDump(DumpType _type, const SysEx& _data)
	{
		bool res;
		switch (_type)
		{
		case DumpType::Single: res = parseSingleDump(_data); break;
		case DumpType::Multi: res = parseMultiDump(_data); break;
		case DumpType::Drum: res = parseDrumDump(_data); break;
		case DumpType::Global: res = parseGlobalDump(_data); break;
		case DumpType::Mode: res = parseModeDump(_data); break;
		default:
			return false;
		}

		if(res && m_isEditBuffer)
			forwardToDevice(_data);
		return res;
	}

	bool State::modifyDump(DumpType _type, const SysEx& _data)
	{
		bool res;
		switch (_type)
		{
		case DumpType::Single: res = modifySingle(_data); break;
		case DumpType::Multi: res = modifyMulti(_data); break;
		case DumpType::Drum: res = modifyDrum(_data); break;
		case DumpType::Global: res = modifyGlobal(_data); break;
		case DumpType::Mode: res = modifyMode(_data); break;
		default:
			return false;
		}
		if(res)
			forwardToDevice(_data);
		return res;
	}

	bool State::requestDumpParameter(DumpType _type, Responses& _responses, const SysEx& _data)
	{
		auto parameterRequestResponse = [&](uint8_t* p)
		{
			if(!p)
				return false;
			auto& data = _responses.emplace_back(_data);
			data.pop_back();
			data.push_back(*p);
			data.push_back(0xf7);
			return true;
		};

		switch (_type)
		{
		case DumpType::Single: return parameterRequestResponse(getSingleParameter(_data));
		case DumpType::Multi: return parameterRequestResponse(getMultiParameter(_data));
		case DumpType::Drum: return parameterRequestResponse(getDrumParameter(_data));
		case DumpType::Global: return parameterRequestResponse(getGlobalParameter(_data));
		case DumpType::Mode: return parameterRequestResponse(getModeParameter(_data));
		default:
			return false;
		}

		// we do not need to forward this to the device as it doesn't support it anyway
	}

	uint8_t State::getGlobalParameter(GlobalParameter _parameter) const
	{
		return m_global[static_cast<uint32_t>(_parameter) + IdxGlobalParamFirst];
	}

	void State::setGlobalParameter(GlobalParameter _parameter, uint8_t _value)
	{
		m_global[static_cast<uint32_t>(_parameter) + IdxGlobalParamFirst] = _value;
	}

	void State::forwardToDevice(const SysEx& _data) const
	{
		if(m_sender == Origin::External)
			m_mq.sendMidi(_data);
	}
}
