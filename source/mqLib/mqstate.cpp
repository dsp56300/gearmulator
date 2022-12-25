#include "mqstate.h"

#include "mqmiditypes.h"

namespace mqLib
{
	State::State()	= default;

	bool State::receive(Responses& _responses, const SysEx& _data, Direction _dir)
	{
		if(_data.size() < 5)
			return false;

		if(_data.front() != 0xf0 || _data.back() != 0xf7)
			return false;

		if(_data[IdxIdWaldorf] != IdWaldorf || _data[IdxIdMicroQ] != IdMicroQ)
			return false;

		const auto cmd = static_cast<SysexCommand>(_data[IdxCommand]);

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

		switch (cmd)
		{
		case SysexCommand::SingleRequest:			return getSingle(_responses, _data);
		case SysexCommand::SingleDump:				return parseSingleDump(_data);
		case SysexCommand::SingleParameterChange:	return modifySingle(_data);
		case SysexCommand::SingleParameterRequest:	return parameterRequestResponse(getSingleParameter(_data));
		case SysexCommand::MultiRequest:			return getMulti(_responses, _data);
		case SysexCommand::MultiDump:				return parseMultiDump(_data);
		case SysexCommand::MultiParameterChange:	return modifyMulti(_data);
		case SysexCommand::MultiParameterRequest:	return parameterRequestResponse(getMultiParameter(_data));
		case SysexCommand::DrumRequest:				return getDrumMap(_responses, _data);
		case SysexCommand::DrumDump:				return parseDrumDump(_data);
		case SysexCommand::DrumParameterChange:		return modifyDrum(_data);
		case SysexCommand::DrumParameterRequest:	return parameterRequestResponse(getDrumParameter(_data));
		case SysexCommand::GlobalRequest:			return getGlobal(_responses, _data);
		case SysexCommand::GlobalDump:				return parseGlobalDump(_data);
		case SysexCommand::GlobalParameterChange:	return modifyGlobal(_data);
		case SysexCommand::GlobalParameterRequest:	return parameterRequestResponse(getGlobalParameter(_data));
/*		case SysexCommand::EmuLCD:
		case SysexCommand::EmuLEDs:
		case SysexCommand::EmuButtons:
		case SysexCommand::EmuRotaries:
			return false;
*/		default: 
			return false;
		}
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
			return nullptr;
		*d = drum;
		return true;
	}

	bool State::parseGlobalDump(const SysEx& _data)
	{
		return convertTo(m_global, _data);
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

	template<size_t Size>
	static uint8_t* getParameter(std::array<uint8_t, Size>& _dump, const SysEx& _data, const uint32_t _offset, const uint32_t _indexH, const uint32_t _indexL)
	{
		if(_indexH >= _data.size() || _indexL >= _data.size())
			return nullptr;

		const auto i = _offset + (static_cast<uint32_t>(_data[_indexH]) << 7) | static_cast<uint32_t>(_data[_indexL]);

		if(i > _dump.size())
			return nullptr;
		return &_dump[i];
	}

	uint8_t* State::getSingleParameter(const SysEx& _data)
	{
		const auto loc = _data[IdxBuffer];

		Single* s = getSingle(MidiBufferNum::SingleEditBufferMultiMode, loc);
		if(!s)
			return nullptr;
		return getParameter(*s, _data, IdxSingleParamFirst, IdxSingleParamIndexH, IdxSingleParamIndexL);
	}

	uint8_t* State::getMultiParameter(const SysEx& _data)
	{
		return getParameter(m_currentMulti, _data, IdxMultiParamFirst, IdxMultiParamIndexH, IdxSingleParamIndexL);
	}

	uint8_t* State::getDrumParameter(const SysEx& _data)
	{
		return getParameter(m_currentDrumMap, _data, IdxDrumParamFirst, IdxDrumParamIndexH, IdxDrumParamIndexL);
	}

	uint8_t* State::getGlobalParameter(const SysEx& _data)
	{
		return getParameter(m_global, _data, IdxGlobalParamFirst, IdxGlobalParamIndexH, IdxGlobalParamIndexL);
	}

	bool State::getSingle(Responses& _responses, const SysEx& _data)
	{
		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		const auto* s = getSingle(buf, loc);
		if(!s)
			return false;
		_responses.push_back(convertTo(*s));
		return true;
	}

	Single* State::getSingle(MidiBufferNum _buf, uint8_t _loc)
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
			return &m_currentInstrumentSingles[getGlobalParameter(GlobalParameter::InstrumentSelection)];
		case MidiBufferNum::SingleEditBufferMultiMode:
//		case MidiBufferNum::EditBufferSingleLayer: (same value as above)
			{
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
		if(!m)
			return false;
		_responses.push_back(convertTo(*m));
		return true;
	}

	Multi* State::getMulti(MidiBufferNum buf, uint8_t loc)
	{
		switch (buf)
		{
		case MidiBufferNum::MultiEditBuffer:
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
		if(!d)
			return nullptr;
		_responses.push_back(convertTo(*d));
		return true;
	}

	DrumMap* State::getDrumMap(const MidiBufferNum _buf, const uint8_t _loc)
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
			return &m_currentDrumMap;
		default:
			return nullptr;
		}
	}

	bool State::getGlobal(Responses& _responses, const SysEx& _data) const
	{
		_responses.push_back(convertTo(m_global));
		return true;
	}

	Global& State::getGlobal()
	{
		return m_global;
	}

	uint8_t State::getGlobalParameter(GlobalParameter _parameter) const
	{
		return m_global[static_cast<uint32_t>(_parameter) + IdxGlobalParamFirst];
	}
}
