#include "mqstate.h"

#include <cassert>
#include <cstring>

#include "mqmiditypes.h"
#include "microq.h"

#include "../synthLib/os.h"
#include "../synthLib/midiToSysex.h"
#include "../synthLib/midiBufferParser.h"
#include "dsp56kEmu/logging.h"

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

		Responses nop;

		for (const auto& message : messages)
			receive(nop, message, Origin::External);

		// if device receives a multi, it switches to multi mode. Switch back to single mode if single mode was requested
		if(getGlobalParameter(GlobalParameter::SingleMultiMode) == 0)
			sendGlobalParameter(GlobalParameter::SingleMultiMode, 0);

		return true;
	}

	bool State::receive(Responses& _responses, const synthLib::SMidiEvent& _data, Origin _sender)
	{
		if(!_data.sysex.empty())
		{
			return receive(_responses, _data.sysex, _sender);
		}

		if (_sender == Origin::Device)
			LOG("Recv: " << HEXN(_data.a, 2) << ' ' << HEXN(_data.b, 2) << ' ' << HEXN(_data.c, 2))

		switch(_data.a & 0xf0)
		{
		case synthLib::M_CONTROLCHANGE:
			switch(_data.b)
			{
			case synthLib::MC_BANKSELECTMSB:
				m_lastBankSelectMSB = _data;
				break;
			case synthLib::MC_BANKSELECTLSB:
				m_lastBankSelectLSB = _data;
				break;
			default:
				return false;
			}
			break;
		case synthLib::M_PROGRAMCHANGE:
			switch(static_cast<BankSelectLSB>(m_lastBankSelectLSB.c))
			{
			case BankSelectLSB::BsDeprecatedSingleBankA:
			case BankSelectLSB::BsDeprecatedSingleBankB:
			case BankSelectLSB::BsDeprecatedSingleBankC:
			case BankSelectLSB::BsSingleBankA:
			case BankSelectLSB::BsSingleBankB:
			case BankSelectLSB::BsSingleBankC:
				if(getGlobalParameter(GlobalParameter::SingleMultiMode) == 0)
					requestSingle(MidiBufferNum::SingleEditBufferSingleMode, MidiSoundLocation::EditBufferCurrentSingle);
				break;
			case BankSelectLSB::BsMultiBank:
				if(getGlobalParameter(GlobalParameter::SingleMultiMode) == 0)
					requestMulti(MidiBufferNum::MultiEditBuffer, 0);
				break;
			default:
				return false;
			}
			break;
		default:
			return false;
		}
		return false;
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
		requestGlobal();
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

		// send default multi
		std::vector<uint8_t> defaultMultiData;
		createSequencerMultiData(defaultMultiData);
		sendMulti(defaultMultiData);

		// accept files up to 300k as larger files might be the OS
		const auto midifile = synthLib::findFile(".mid", 0, 300 * 1024);
		if(!midifile.empty())
		{
			std::vector<uint8_t> sysex;
			synthLib::MidiToSysex::readFile(sysex, midifile.c_str());
			if(!sysex.empty())
				loadState(sysex);
		}

		// switch to Single mode as the multi dump causes it to go to Multi mode
		sendGlobalParameter(GlobalParameter::SingleMultiMode, 0);

		if(isValid(m_romSingles[0]))
		{
			auto dump = convertTo(m_romSingles[0]);

			dump[IdxBuffer]   = static_cast<uint8_t>(MidiBufferNum::SingleEditBufferSingleMode);
			dump[IdxLocation] = 0;

			forwardToDevice(dump);
		}
	}

	bool State::getState(std::vector<uint8_t>& _state, synthLib::StateType _type) const
	{
		append(_state, m_global);

		const auto isMultiMode = getGlobalParameter(GlobalParameter::SingleMultiMode) != 0;

//		append(_state, m_currentDrumMap);

		append(_state, m_currentMulti);

		for(size_t i=0; i<m_currentMultiSingles.size(); ++i)
		{
			const auto& s = (isMultiMode || i >= m_currentInstrumentSingles.size()) ? m_currentMultiSingles[i] : m_currentInstrumentSingles[i];
			append(_state, s);
		}

//		for (const auto& s : m_currentDrumMapSingles)
//			append(_state, s);

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

		if(*p == _data[IdxGlobalParamValue])
			return true;

		*p = _data[IdxGlobalParamValue];

		// if the play mode is changed, request the edit buffer for the first single again, because on the mQ, that edit buffer is shared between multi & single
		const auto param = (_data[IdxGlobalParamIndexH] << 7) | _data[IdxGlobalParamIndexL];

		if(param == static_cast<uint32_t>(GlobalParameter::SingleMultiMode))
		{
			const auto v = _data[IdxGlobalParamValue];

			requestSingle(v ? MidiBufferNum::SingleEditBufferMultiMode : MidiBufferNum::SingleEditBufferSingleMode, MidiSoundLocation::EditBufferFirstMultiSingle);
		}

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

		Single* s = getSingle(getGlobalParameter(GlobalParameter::SingleMultiMode) ? MidiBufferNum::SingleEditBufferMultiMode : MidiBufferNum::SingleEditBufferSingleMode, loc);
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
			return m_currentInstrumentSingles.data();//getGlobalParameter(GlobalParameter::InstrumentSelection)];
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
				if(_loc < m_currentInstrumentSingles.size())
					return &m_currentInstrumentSingles[_loc];
				if (_loc < m_currentMultiSingles.size())
					return &m_currentMultiSingles[_loc];
				return nullptr;
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
		return true;
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

		if(res)
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
		if(m_sender != Origin::External)
			return;

		sendSysex(_data);
	}

	void State::requestGlobal() const
	{
		sendSysex({0xf0, IdWaldorf, IdMicroQ, IdDeviceOmni, static_cast<uint8_t>(SysexCommand::GlobalRequest), 0xf7});
	}

	void State::requestSingle(MidiBufferNum _buf, MidiSoundLocation _location) const
	{
		sendSysex({0xf0, IdWaldorf, IdMicroQ, IdDeviceOmni, static_cast<uint8_t>(SysexCommand::SingleRequest), static_cast<uint8_t>(_buf), static_cast<uint8_t>(_location), 0xf7});
	}

	void State::requestMulti(MidiBufferNum _buf, uint8_t _location) const
	{
		sendSysex({0xf0, IdWaldorf, IdMicroQ, IdDeviceOmni, static_cast<uint8_t>(SysexCommand::MultiRequest), static_cast<uint8_t>(_buf), _location, 0xf7});
	}

	inline void State::sendMulti(const std::vector<uint8_t>& _multiData) const
	{
		std::vector<uint8_t> data = { 0xf0, IdWaldorf, IdMicroQ, IdDeviceOmni, static_cast<uint8_t>(SysexCommand::MultiDump), static_cast<uint8_t>(MidiBufferNum::DeprecatedMultiBankInternal), 0};
		data.insert(data.end(), _multiData.begin(), _multiData.end());

		uint8_t checksum = 0;
		for(size_t i=4; i<data.size(); ++i)
			checksum += data[i];
		data.push_back(checksum & 0x7f);
		data.push_back(0xf7);
		sendSysex(data);
	}

	void State::sendGlobalParameter(GlobalParameter _param, uint8_t _value)
	{
		setGlobalParameter(_param, _value);

		const auto p = static_cast<uint8_t>(_param);

		sendSysex({0xf0, IdWaldorf, IdMicroQ, IdDeviceOmni, static_cast<uint8_t>(SysexCommand::GlobalParameterChange),
			static_cast<uint8_t>(p >> 7), static_cast<uint8_t>(p & 0x7f), _value, 0xf7});
	}

	void State::sendSysex(const std::initializer_list<uint8_t>& _data) const
	{
		synthLib::SMidiEvent e;
		e.sysex = _data;
		m_mq.sendMidiEvent(e);
	}

	void State::sendSysex(const SysEx& _data) const
	{
		synthLib::SMidiEvent e;
		e.sysex = _data;
		m_mq.sendMidiEvent(e);
	}

	void State::createSequencerMultiData(std::vector<uint8_t>& _data)
	{
		static_assert(
			(static_cast<uint32_t>(MultiParameter::Inst15) - static_cast<uint32_t>(MultiParameter::Inst0)) ==
			(static_cast<uint32_t>(MultiParameter::Inst1) - static_cast<uint32_t>(MultiParameter::Inst0)) * 15,
			"we need a consecutive offset");

		_data.assign(static_cast<uint32_t>(mqLib::MultiParameter::Count), 0);

		constexpr char name[] = "From TUS with <3";
		static_assert(std::size(name) == 17, "wrong name length");
		memcpy(&_data[static_cast<uint32_t>(MultiParameter::Name00)], name, sizeof(name) - 1);

		auto setParam = [&](MultiParameter _param, const uint8_t _value)
		{
			_data[static_cast<uint32_t>(_param)] = _value;
		};

		auto setInstParam = [&](const uint8_t _instIndex, const MultiParameter _param, const uint8_t _value)
		{
			auto index = static_cast<uint32_t>(MultiParameter::Inst0) + (static_cast<uint32_t>(MultiParameter::Inst1) - static_cast<uint32_t>(MultiParameter::Inst0)) * _instIndex;
			index += static_cast<uint32_t>(_param) - static_cast<uint32_t>(MultiParameter::Inst0);
			_data[index] = _value;
		};

		setParam(MultiParameter::Volume, 127);						// max volume

		setParam(MultiParameter::ControlW, 121);					// global
		setParam(MultiParameter::ControlX, 121);					// global
		setParam(MultiParameter::ControlY, 121);					// global
		setParam(MultiParameter::ControlZ, 121);					// global

		for (uint8_t i = 0; i < 16; ++i)
		{
			setInstParam(i, MultiParameter::Inst0SoundBank, 0);	    // bank A
			setInstParam(i, MultiParameter::Inst0SoundNumber, i);	// sound number i
			setInstParam(i, MultiParameter::Inst0MidiChannel, 2+i);	// midi channel i
			setInstParam(i, MultiParameter::Inst0Volume, 127);		// max volume
			setInstParam(i, MultiParameter::Inst0Transpose, 64);	// no transpose
			setInstParam(i, MultiParameter::Inst0Detune, 64);		// no detune
			setInstParam(i, MultiParameter::Inst0Output, 0);		// main out
			setInstParam(i, MultiParameter::Inst0Flags, 3);			// RX = Local+MIDI / TX = off / Engine = Play
			setInstParam(i, MultiParameter::Inst0Pan, 64);			// center
			setInstParam(i, MultiParameter::Inst0Pattern, 0);		// no pattern
			setInstParam(i, MultiParameter::Inst0VeloLow, 1);		// full velocity range
			setInstParam(i, MultiParameter::Inst0VeloHigh, 127);
			setInstParam(i, MultiParameter::Inst0KeyLow, 0);		// full key range
			setInstParam(i, MultiParameter::Inst0KeyHigh, 127);
			setInstParam(i, MultiParameter::Inst0MidiRxFlags, 63);	// enable Pitchbend, Modwheel, Aftertouch, Sustain, Button 1/2, Program Change
		}
	}
}
