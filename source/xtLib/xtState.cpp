#include "xtState.h"

#include <cassert>
#include <set>

#include "xtMidiTypes.h"
#include "xt.h"
#include "xtWavePreview.h"

#include "synthLib/os.h"
#include "synthLib/midiToSysex.h"
#include "synthLib/midiBufferParser.h"

#include "dsp56kEmu/logging.h"

namespace xt
{
	static_assert(std::size(State::Dumps) == static_cast<uint32_t>(State::DumpType::Count), "data definition missing");

	State::State(Xt& _xt, WavePreview& _wavePreview) : m_xt(_xt), m_wavePreview(_wavePreview)
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

		return true;
	}

	bool State::getState(std::vector<uint8_t>& _state, synthLib::StateType _type) const
	{
		append(_state, m_mode, ~0u);
		append(_state, m_global, wLib::IdxCommand);

		// add all waves and tables that are used by the singles
		std::set<TableId> tableIds;

		auto addTableId = [&](TableId id)
		{
			if(wave::isReadOnly(id))
				return false;

			const auto idx = id.rawId();
			if(idx >= m_tables.size())
				return false;

			if(!isValid(m_tables[idx]))
				return false;
			tableIds.insert(id);
			return true;
		};

		for (const auto& s  : m_currentInstrumentSingles)
		{
			const auto table = getWavetableFromSingleDump({s.begin(), s.end()});
			addTableId(table);
		}
		for (const auto& s  : m_currentMultiSingles)
		{
			const auto table = getWavetableFromSingleDump({s.begin(), s.end()});
			addTableId(table);
		}

		std::set<WaveId> waveIds;
		for (const TableId id : tableIds)
		{
			if(wave::isReadOnly(id))
				continue;

			const auto idx = id.rawId();
			if(idx >= m_tables.size())
				continue;

			const auto& t = m_tables[idx];
			if(!isValid(t))
				continue;

			TableData table;
			parseTableData(table, {t.begin(), t.end()});

			for (const auto& waveId : table)
			{
				if(wave::isReadOnly(waveId))
					continue;
				const auto waveIdx = waveId.rawId();
				if(waveIdx >= m_waves.size())
					continue;
				const auto& wave = m_waves[waveIdx];
				if(!isValid(wave))
					continue;
				waveIds.insert(waveId);
			}
		}

		for (const auto& waveId : waveIds)
			append(_state, m_waves[waveId.rawId()], wLib::IdxCommand);

		for (const auto& tableId : tableIds)
			append(_state, m_tables[tableId.rawId()], wLib::IdxCommand);

		const auto multiMode = isMultiMode();

		// if we are in multimode, write multis last, otherwise, write singles last
		// This causes the relevant things to be activated last when loading
		if(multiMode)
		{
			for (const auto& s: m_currentInstrumentSingles)
				append(_state, s, IdxSingleChecksumStart);
			append(_state, m_currentMulti, IdxMultiChecksumStart);
			for (const auto& s: m_currentMultiSingles)
				append(_state, s, IdxSingleChecksumStart);
		}
		else
		{
			append(_state, m_currentMulti, IdxMultiChecksumStart);
			for (const auto& s: m_currentMultiSingles)
				append(_state, s, IdxSingleChecksumStart);
			for (const auto& s: m_currentInstrumentSingles)
				append(_state, s, IdxSingleChecksumStart);
		}

		return !_state.empty();
	}

	bool State::receive(Responses& _responses, const synthLib::SMidiEvent& _data, Origin _sender)
	{
		if(!_data.sysex.empty())
		{
			return receive(_responses, _data.sysex, _sender);
		}

		if (_sender == Origin::Device)
			LOG("Recv: " << HEXN(_data.a, 2) << ' ' << HEXN(_data.b, 2) << ' ' << HEXN(_data.c, 2));

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
			/*
			switch(static_cast<BankSelectLSB>(m_lastBankSelectLSB.c))
			{
			case BankSelectLSB::BsDeprecatedSingleBankA:
			case BankSelectLSB::BsDeprecatedSingleBankB:
			case BankSelectLSB::BsDeprecatedSingleBankC:
			case BankSelectLSB::BsSingleBankA:
			case BankSelectLSB::BsSingleBankB:
			case BankSelectLSB::BsSingleBankC:
				if(getGlobalParameter(GlobalParameter::SingleMultiMode) == 0)
					requestSingle(LocationH::SingleEditBufferSingleMode, MidiSoundLocation::EditBufferCurrentSingle);
				break;
			case BankSelectLSB::BsMultiBank:
				if(getGlobalParameter(GlobalParameter::SingleMultiMode) != 0)
					requestMulti(LocationH::MultiEditBuffer, 0);
				break;
			default:
				return false;
			}
			*/
			break;
		default:
			return false;
		}
		return false;
	}

	bool State::receive(Responses& _responses, const SysEx& _data, const Origin _sender)
	{
		if(_data.size() == Mw1::g_singleDumpLength)
		{
			m_sender = _sender;
			forwardToDevice(_data);

			// MW1 dump doesn't contain any information about which part or bank it is loaded into, the first
			// part is always the target
			// Invalidate the currently cached single. We cannot do the conversion here, the hardware has to.
			// The editor needs to request the single after sending a MW1 dump, which will fill our cache again
			if(isMultiMode())
				m_currentMultiSingles[0].fill(0);
			else
				m_currentInstrumentSingles.front().fill(0);
			return true;
		}

		const auto cmd = getCommand(_data);

		if(cmd == SysexCommand::Invalid)
			return false;

		m_sender = _sender;
		m_isEditBuffer = false;

		switch (cmd)
		{
		case SysexCommand::SingleRequest:			return getDump(DumpType::Single, _responses, _data);
		case SysexCommand::MultiRequest:			return getDump(DumpType::Multi,_responses, _data);
		case SysexCommand::GlobalRequest:			return getDump(DumpType::Global, _responses, _data);
		case SysexCommand::ModeRequest:				return getDump(DumpType::Mode, _responses, _data);
		case SysexCommand::WaveRequest:				return getDump(DumpType::Wave, _responses, _data);
		case SysexCommand::WaveCtlRequest:			return getDump(DumpType::Table, _responses, _data);

		case SysexCommand::SingleDump:				return parseDump(DumpType::Single, _data);
		case SysexCommand::MultiDump:				return parseDump(DumpType::Multi, _data);
		case SysexCommand::GlobalDump:				return parseDump(DumpType::Global, _data);
		case SysexCommand::ModeDump:				return parseDump(DumpType::Mode, _data);
		case SysexCommand::WaveDump:				return parseDump(DumpType::Wave, _data);
		case SysexCommand::WaveCtlDump:				return parseDump(DumpType::Table, _data);

		case SysexCommand::SingleParameterChange:	return modifyDump(DumpType::Single, _data);
		case SysexCommand::MultiParameterChange:	return modifyDump(DumpType::Multi, _data);
		case SysexCommand::GlobalParameterChange:	return modifyDump(DumpType::Global, _data);
		case SysexCommand::ModeParameterChange:		return modifyDump(DumpType::Mode, _data);

		case SysexCommand::WaveDumpP:				return m_wavePreview.receiveWave(_data);
		case SysexCommand::WaveCtlDumpP:			return m_wavePreview.receiveWaveControlTable(_data);
		case SysexCommand::WavePreviewMode:			return m_wavePreview.receiveWavePreviewMode(_data);

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
		requestMode();

		synthLib::MidiBufferParser parser;
		Responses unused;
		std::vector<uint8_t> midi;
		std::vector<synthLib::SMidiEvent> events;

		while(!isValid(m_global) || !isValid(m_mode))
		{
			m_xt.process(8);
			midi.clear();
			m_xt.receiveMidi(midi);
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

		auto setParam = [&](const GlobalParameter _param, const uint8_t _value)
		{
			sendGlobalParameter(_param, _value);
		};

		setParam(GlobalParameter::StartupSoundbank, 0);			// First bank
		setParam(GlobalParameter::StartupSoundNum, 0);			// First sound
		setParam(GlobalParameter::StartupMultiNumber, 0);		// First Multi

		setParam(GlobalParameter::ProgramChangeMode, 0);		// single
		setParam(GlobalParameter::MasterTune, 64);				// 440 Hz
		setParam(GlobalParameter::Transpose, 64);				// +/- 0
		setParam(GlobalParameter::ParameterSend, 2);			// SysEx
		setParam(GlobalParameter::ParameterReceive, 1);			// on
		setParam(GlobalParameter::ArpNoteOutChannel, 0);		// off
		setParam(GlobalParameter::MidiClockOutput, 0);			// off
		setParam(GlobalParameter::MidiChannel, 1);				// omni
		setParam(GlobalParameter::DeviceId, 0);					// 0
		setParam(GlobalParameter::InputGain, 3);				// 4

		receive(unused, convertTo(m_global), Origin::External);
	}

	bool State::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return loadState(_state);
	}

	TableId State::getWavetableFromSingleDump(const SysEx& _single)
	{
		constexpr auto wavetableIndex = IdxSingleParamFirst + static_cast<uint32_t>(SingleParameter::Wavetable);

		if(wavetableIndex >= _single.size())
			return TableId::invalid();

		return TableId(_single[wavetableIndex]);
	}

	bool State::parseSingleDump(const SysEx& _data)
	{
		Single single;

		if(!convertTo(single, _data))
			return false;

		const auto buf = static_cast<LocationH>(_data[wLib::IdxBuffer]);
		const auto loc = _data[wLib::IdxLocation];

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

		const auto buf = static_cast<LocationH>(_data[wLib::IdxBuffer]);
		const auto loc = _data[wLib::IdxLocation];

		auto* m = getMulti(buf, loc);
		if(!m)
			return false;
		*m = multi;
		return true;
	}

	bool State::parseGlobalDump(const SysEx& _data)
	{
		return convertTo(m_global, _data);
	}

	bool State::parseModeDump(const SysEx& _data)
	{
		if(!convertTo(m_mode, _data))
			return false;
		onPlayModeChanged();
		return true;
	}

	bool State::parseWaveDump(const SysEx& _data)
	{
		const auto idx = (static_cast<uint32_t>(_data[IdxWaveIndexH]) << 7u) | static_cast<uint32_t>(_data[IdxWaveIndexL]);

		if(idx >= m_waves.size())
			return false;

		if(!convertTo(m_waves[idx], _data))
			return false;

		return true;
	}

	bool State::parseTableDump(const SysEx& _data)
	{
		const auto idx = (static_cast<uint32_t>(_data[IdxWaveIndexH]) << 7u) | static_cast<uint32_t>(_data[IdxWaveIndexL]);

		if(idx >= m_tables.size())
			return false;

		if(!convertTo(m_tables[idx], _data))
			return false;

		return true;
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

	bool State::modifyGlobal(const SysEx& _data)
	{
		auto* p = getGlobalParameter(_data);
		if(!p)
			return false;

		if(*p == _data[IdxGlobalParamValue])
			return true;

		*p = _data[IdxGlobalParamValue];

		return true;
	}

	bool State::modifyMode(const SysEx& _data)
	{
		auto* p = getModeParameter(_data);
		if(!p)
			return false;

		*p = _data[IdxModeParamValue];

		onPlayModeChanged();

		return true;
	}

	namespace
	{
		template<size_t Size>
		uint8_t* getParameter(std::array<uint8_t, Size>& _dump, const SysEx& _data, State::DumpType _type)
		{
			const auto& dump = State::Dumps[static_cast<uint32_t>(_type)];

			if(dump.idxParamIndexH >= _data.size() || dump.idxParamIndexL >= _data.size())
				return nullptr;

			auto i = dump.firstParamIndex;
			if (dump.idxParamIndexH != dump.idxParamIndexL)
				i += static_cast<uint32_t>(_data[dump.idxParamIndexH]) << 7;
			i += static_cast<uint32_t>(_data[dump.idxParamIndexL]);

			if(i > _dump.size())
				return nullptr;
			return &_dump[i];
		}
	}

	uint8_t* State::getSingleParameter(const SysEx& _data)
	{
		const auto loc = _data[wLib::IdxBuffer];

		Single* s = getSingle(isMultiMode() ? LocationH::SingleEditBufferMultiMode : LocationH::SingleEditBufferSingleMode, loc);
		if(!s)
			return nullptr;
		return getParameter(*s, _data, DumpType::Single);
	}

	uint8_t* State::getMultiParameter(const SysEx& _data)
	{
		const auto& dump = Dumps[static_cast<uint8_t>(DumpType::Multi)];

		const auto idxH = _data[dump.idxParamIndexH];
		const auto idxL = _data[dump.idxParamIndexL];
//		const auto val = _data[dump.idxParamValue];

		if(idxH == 0x20)
			return &m_currentMulti[dump.firstParamIndex + idxL];

		constexpr auto inst0 = static_cast<uint8_t>(MultiParameter::Inst0First);
		constexpr auto inst1 = static_cast<uint8_t>(MultiParameter::Inst1First);

		const auto idx = dump.firstParamIndex + inst0 + idxH * (inst1 - inst0) + idxL;

		return &m_currentMulti[idx];
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
		const auto buf = static_cast<LocationH>(_data[wLib::IdxBuffer]);
		const auto loc = _data[wLib::IdxLocation];

		const auto* s = getSingle(buf, loc);
		if(!s || !isValid(*s))
			return false;
		_responses.push_back(convertTo(*s));
		return true;
	}

	State::Single* State::getSingle(LocationH _buf, uint8_t _loc)
	{
		switch (_buf)
		{
		case LocationH::SingleBankA:
			if(_loc >= 128)
				return nullptr;
			return &m_romSingles[_loc];
		case LocationH::SingleBankB:
			if(_loc >= 128)
				return nullptr;
			return &m_romSingles[_loc + 100];
		case LocationH::SingleEditBufferSingleMode:
			m_isEditBuffer = true;
			return m_currentInstrumentSingles.data();
		case LocationH::SingleEditBufferMultiMode:
			{
				m_isEditBuffer = true;
				if(_loc >= m_currentMultiSingles.size())
					return nullptr;
				return &m_currentMultiSingles[_loc];
		}
		default:
			return nullptr;
		}
	}

	bool State::getMulti(Responses& _responses, const SysEx& _data)
	{
		const auto buf = static_cast<LocationH>(_data[wLib::IdxBuffer]);
		const auto loc = _data[wLib::IdxLocation];

		const auto* m = getMulti(buf, loc);
		if(!m || !isValid(*m))
			return false;
		_responses.push_back(convertTo(*m));
		return true;
	}

	State::Multi* State::getMulti(LocationH buf, uint8_t loc)
	{
		switch (buf)
		{
		case LocationH::MultiDumpMultiEditBuffer:
			m_isEditBuffer = true;
			return &m_currentMulti;
		case LocationH::MultiBankA:
			if(loc >= m_romMultis.size())
				return nullptr;
			return &m_romMultis[loc];
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

	bool State::getWave(Responses& _responses, const SysEx& _data)
	{
		const auto idx = static_cast<uint16_t>((static_cast<uint16_t>(_data[IdxWaveIndexH]) << 7u) | static_cast<uint16_t>(_data[IdxWaveIndexL]));

		auto* w = getWave(WaveId(idx));
		if(!w || !isValid(*w))
			return false;
		_responses.emplace_back(w->begin(), w->end());
		return true;
	}

	State::Wave* State::getWave(const WaveId _id)
	{
		const auto idx = _id.rawId();
		if(idx >= m_waves.size())
			return nullptr;
		return &m_waves[idx];
	}

	bool State::getTable(Responses& _responses, const SysEx& _data)
	{
		const auto idx = static_cast<uint16_t>((static_cast<uint16_t>(_data[IdxWaveIndexH]) << 7u) | static_cast<uint16_t>(_data[IdxWaveIndexL]));

		auto* t = getTable(TableId(idx));
		if(!t || !isValid(*t))
			return false;
		_responses.emplace_back(t->begin(), t->end());
		return true;
	}

	State::Table* State::getTable(const TableId _id)
	{
		const auto idx = _id.rawId();
		if(idx >= m_tables.size())
			return nullptr;
		return &m_tables[idx];
	}

	bool State::getDump(const DumpType _type, Responses& _responses, const SysEx& _data)
	{
		bool res;

		switch (_type)
		{
		case DumpType::Single: res = getSingle(_responses, _data); break;
		case DumpType::Multi: res = getMulti(_responses, _data); break;
		case DumpType::Global: res = getGlobal(_responses); break;
		case DumpType::Mode: res = getMode(_responses); break;
		case DumpType::Wave: res = getWave(_responses, _data); break;
		case DumpType::Table: res = getTable(_responses, _data); break;
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
		case DumpType::Global: res = parseGlobalDump(_data); break;
		case DumpType::Mode: res = parseModeDump(_data); break;
		case DumpType::Wave: res = parseWaveDump(_data); break;
		case DumpType::Table: res = parseTableDump(_data); break;
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
		case DumpType::Global: res = modifyGlobal(_data); break;
		case DumpType::Mode: res = modifyMode(_data); break;
		default:
			return false;
		}
		if(res)
			forwardToDevice(_data);
		return res;
	}

	uint8_t State::getGlobalParameter(const GlobalParameter _parameter) const
	{
		return m_global[static_cast<uint32_t>(_parameter) + IdxGlobalParamFirst];
	}

	void State::setGlobalParameter(GlobalParameter _parameter, uint8_t _value)
	{
		m_global[static_cast<uint32_t>(_parameter) + IdxGlobalParamFirst] = _value;
	}

	uint8_t State::getModeParameter(const ModeParameter _parameter) const
	{
		return m_mode[static_cast<uint32_t>(_parameter) + IdxModeParamFirst];
	}

	SysexCommand State::getCommand(const SysEx& _data)
	{
		if (_data.size() < 5)
			return SysexCommand::Invalid;

		if (_data.front() != 0xf0 || _data.back() != 0xf7)
			return SysexCommand::Invalid;

		if (_data[wLib::IdxIdWaldorf] != wLib::IdWaldorf || _data[wLib::IdxIdMachine] != IdMw2)
			return SysexCommand::Invalid;

		return static_cast<SysexCommand>(_data[wLib::IdxCommand]);
	}

	void State::forwardToDevice(const SysEx& _data) const
	{
		if(m_sender != Origin::External)
			return;

		sendSysex(_data);
	}

	void State::requestGlobal() const
	{
		sendSysex({0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(SysexCommand::GlobalRequest), 0xf7});
	}

	void State::requestMode() const
	{
		sendSysex({0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(SysexCommand::ModeRequest), 0xf7});
	}

	void State::requestSingle(LocationH _buf, uint8_t _location) const
	{
		sendSysex({0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(SysexCommand::SingleRequest), static_cast<uint8_t>(_buf), static_cast<uint8_t>(_location), 0xf7});
	}

	void State::requestMulti(LocationH _buf, uint8_t _location) const
	{
		sendSysex({0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(SysexCommand::MultiRequest), static_cast<uint8_t>(_buf), _location, 0xf7});
	}

	inline void State::sendMulti(const std::vector<uint8_t>& _multiData) const
	{
		std::vector<uint8_t> data = { 0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(SysexCommand::MultiDump), static_cast<uint8_t>(LocationH::MultiBankA), 0};
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

		sendSysex({0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(SysexCommand::GlobalParameterChange),
			static_cast<uint8_t>(p >> 7), static_cast<uint8_t>(p & 0x7f), _value, 0xf7});
	}

	void State::sendMultiParameter(const uint8_t _instrument, MultiParameter _param, const uint8_t _value)
	{
		const SysEx sysex{0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(SysexCommand::MultiParameterChange),
			_instrument, static_cast<uint8_t>(static_cast<uint8_t>(_param) - static_cast<uint8_t>(MultiParameter::Inst0First)), _value, 0xf7};

		Responses responses;
		receive(responses, sysex, Origin::External);
	}

	void State::sendSysex(const std::initializer_list<uint8_t>& _data) const
	{
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Internal);
		e.sysex = _data;
		m_xt.sendMidiEvent(e);
	}

	void State::sendSysex(const SysEx& _data) const
	{
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Internal);
		e.sysex = _data;
		m_xt.sendMidiEvent(e);
	}

	void State::createSequencerMultiData(std::vector<uint8_t>& _data)
	{
		assert(false);
		/*
		static_assert(
			(static_cast<uint32_t>(MultiParameter::Inst15) - static_cast<uint32_t>(MultiParameter::Inst0)) ==
			(static_cast<uint32_t>(MultiParameter::Inst1) - static_cast<uint32_t>(MultiParameter::Inst0)) * 15,
			"we need a consecutive offset");

		_data.assign(static_cast<uint32_t>(xt::MultiParameter::Count), 0);

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
		*/
	}

	void State::parseWaveData(WaveData& _wave, const SysEx& _sysex)
	{
		/*
			mw2_sysex.pdf:

			"A Wave consists of 128 eight Bit samples, but only the first 64 of them are
			stored/transmitted, the second half is same as first except the values are
			negated and the order is reversed:

			Wave[64+n] = -Wave[63-n] for n=0..63

			Note that samples are not two's complement format, to get a signed byte,
			the most significant bit must be flipped:

			signed char s = Wave[n] ^ 0x80"
		*/

		constexpr auto off = 7;

		for(uint32_t i=0; i<_wave.size()>>1; ++i)
		{
			const auto idx = off + (i<<1);
			auto sample = (_sysex[idx]) << 4 | _sysex[idx+1];
			sample = sample ^ 0x80;

			_wave[i] = static_cast<int8_t>(sample);
			_wave[127-i] = static_cast<int8_t>(-sample);
		}
	}

	SysEx State::createWaveData(const WaveData& _wave, const uint16_t _waveIndex, const bool _preview)
	{
		const auto hh = static_cast<uint8_t>(_waveIndex >> 7);
		const auto ll = static_cast<uint8_t>(_waveIndex & 0x7f);

		const std::initializer_list<uint8_t> header{0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(_preview ? SysexCommand::WaveDumpP : SysexCommand::WaveDump), hh, ll};
		SysEx sysex{header};
		sysex.reserve(sysex.size() + _wave.size());

		for(uint32_t i=0; i<_wave.size()>>1; ++i)
		{
			const int sample = static_cast<uint8_t>(_wave[i] ^ 0x80);

			sysex.push_back(static_cast<uint8_t>(sample >> 4));
			sysex.push_back(static_cast<uint8_t>(sample & 0xf));
		}

		sysex.push_back(0);
		sysex.push_back(0xf7);

		updateChecksum(sysex, static_cast<uint32_t>(std::size(header)));

		return sysex;
	}

	WaveData State::createinterpolatedTable(const WaveData& _a, const WaveData& _b, uint16_t _indexA, uint16_t _indexB, uint16_t _indexTarget)
	{
		assert(_indexB > _indexA);
		assert(_indexTarget >= _indexA && _indexTarget <= _indexB);

		xt::WaveData result;

		const auto indexDelta = _indexB - _indexA;
		const auto targetDelta = _indexTarget - _indexA;

		for(size_t i=0; i<_a.size(); ++i)
		{
			auto d = _b[i] - _a[i];
			d *= targetDelta;
			d /= indexDelta;
			d += _a[i];
			result[i] = static_cast<int8_t>(d);
		}
		return result;
	}

	void State::parseTableData(TableData& _table, const SysEx& _sysex)
	{
		constexpr uint32_t off = 7;

		for(uint32_t i=0; i<_table.size(); ++i)
		{
			const auto i4 = i<<2;

			auto waveIdx = _sysex[i4+off] << 12;
			waveIdx |= _sysex[i4+off+1] << 8;
			waveIdx |= _sysex[i4+off+2] << 4;
			waveIdx |= _sysex[i4+off+3];

			_table[i] = WaveId(static_cast<uint16_t>(waveIdx));
		}
	}

	SysEx State::createTableData(const TableData& _table, const uint32_t _tableIndex, const bool _preview)
	{
		const auto hh = static_cast<uint8_t>(_tableIndex >> 7);
		const auto ll = static_cast<uint8_t>(_tableIndex & 0x7f);

		const std::initializer_list<uint8_t> header{0xf0, wLib::IdWaldorf, IdMw2, wLib::IdDeviceOmni, static_cast<uint8_t>(_preview ? SysexCommand::WaveCtlDumpP : SysexCommand::WaveCtlDump), hh, ll};
		SysEx sysex{header};
		sysex.reserve(sysex.size() + _table.size() * 4 + 2);

		for (const auto& e : _table)
		{
			const auto waveId = e.rawId();

			sysex.push_back((waveId >> 12) & 0xf);
			sysex.push_back((waveId >> 8 ) & 0xf);
			sysex.push_back((waveId >> 4 ) & 0xf);
			sysex.push_back((waveId      ) & 0xf);
		}

		sysex.push_back(0);
		sysex.push_back(0xf7);

		updateChecksum(sysex, static_cast<uint32_t>(std::size(header)));

		return sysex;
	}

	void State::splitCombinedPatch(std::vector<SysEx>& _dumps, const SysEx& _combinedSingle)
	{
		if(getCommand(_combinedSingle) != SysexCommand::SingleDump)
			return;

		constexpr auto singleSize = std::tuple_size_v<Single>;
		constexpr auto tableSize = std::tuple_size_v<Table>;
		constexpr auto waveSize = std::tuple_size_v<Wave>;

		if(_combinedSingle.size() == singleSize)
			return;

		if(_combinedSingle.size() < singleSize + tableSize - 2)
			return;

		auto& single = _dumps.emplace_back();
		size_t offBegin = 0;
		size_t offEnd = offBegin + singleSize - 1;
		single.assign(_combinedSingle.begin() + static_cast<ptrdiff_t>(offBegin), _combinedSingle.begin() + static_cast<ptrdiff_t>(offEnd));
		single.push_back(0xf7);

		auto& table = _dumps.emplace_back();

		offBegin = offEnd;
		offEnd += tableSize - 2;
		table.push_back(0xf0);
		table.insert(table.end(), _combinedSingle.begin() + static_cast<ptrdiff_t>(offBegin), _combinedSingle.begin() + static_cast<ptrdiff_t>(offEnd));
		table.push_back(0xf7);

		while(_combinedSingle.size() - offEnd >= waveSize - 2)
		{
			offBegin = offEnd;
			offEnd += waveSize - 2;

			auto& wave = _dumps.emplace_back();
			wave.push_back(0xf0);
			wave.insert(wave.end(), _combinedSingle.begin() + static_cast<ptrdiff_t>(offBegin), _combinedSingle.begin() + static_cast<ptrdiff_t>(offEnd));
			wave.push_back(0xf7);
		}
	}

	SysEx State::createCombinedPatch(const std::vector<SysEx>& _dumps)
	{
		uint32_t singleCount = 0;
		uint32_t tableCount = 0;

		std::vector<SysEx> waves;

		SysEx single;
		SysEx table;

		for (auto& dump : _dumps)
		{
			switch(getCommand(dump))
			{
			case SysexCommand::SingleDump:
				++singleCount;
				single = dump;
				break;
			case SysexCommand::WaveCtlDump:
				++tableCount;
				table = dump;
				break;
			case SysexCommand::WaveDump:
				waves.push_back(dump);
				break;
			default:;
			}
		}

		if(!tableCount && waves.empty())
			return single;

		if(tableCount > 1)
			return {};

		// a combined single is a single dump + a table dump + an arbitrary number of wave dumps in one sysex, i.e. f0/f7 are stripped from the individual dumps
		single.pop_back();

		single.insert(single.end(), table.begin()+1, table.end());

		for (const auto& wave : waves)
			single.insert(single.end(), wave.begin()+1, wave.end()-1);

		single.push_back(0xf7);

		return single;
	}

	void State::onPlayModeChanged()
	{
		// if the play mode is changed, force a re-request of the edit buffer for the first single again, because on the device, that edit buffer is shared between multi & single
		m_currentMultiSingles[0][0] = 0;
		m_currentInstrumentSingles[0][0] = 0;

		// also, as the multi is not valid if the machine is not in multi mode, invalidate the existing data to force a re-request from the device
		if(isMultiMode())
			m_currentMulti[0] = 0;
	}
}
