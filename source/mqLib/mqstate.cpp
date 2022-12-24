#include "mqstate.h"

#include "mqmiditypes.h"

namespace mqLib
{
	State::State()	= default;

	bool State::receive(std::vector<SysEx>& _responses, const SysEx& _data, Direction _dir)
	{
		if(_data.size() < 5)
			return false;

		if(_data.front() != 0xf0 || _data.back() != 0xf7)
			return false;

		if(_data[IdxIdWaldorf] != IdWaldorf || _data[IdxIdMicroQ] != IdMicroQ)
			return false;

		const auto cmd = static_cast<SysexCommand>(_data[IdxCommand]);

		switch (cmd)
		{
		case SysexCommand::SingleRequest: break;
		case SysexCommand::SingleDump:
			return parseSingleDump(_data);
		case SysexCommand::SingleParameterChange:
			break;
		case SysexCommand::SingleParameterRequest:
			break;
		case SysexCommand::MultiRequest: break;
		case SysexCommand::MultiDump:
			return parseMultiDump(_data);
		case SysexCommand::MultiParameterChange:
			break;
		case SysexCommand::MultiParameterRequest:
			break;
		case SysexCommand::DrumRequest: break;
		case SysexCommand::DrumDump:
			return parseDrumDump(_data);
		case SysexCommand::DrumParameterChange:
			break;
		case SysexCommand::DrumParameterRequest:
			break;
		case SysexCommand::GlobalRequest:
			break;
		case SysexCommand::GlobalDump:
			return parseGlobalDump(_data);
		case SysexCommand::GlobalParameterChange:
			break;
		case SysexCommand::GlobalParameterRequest: 
			break;
		case SysexCommand::EmuLCD:
		case SysexCommand::EmuLEDs:
		case SysexCommand::EmuButtons:
		case SysexCommand::EmuRotaries:
			return false;
		default: 
			return false;
		}

		return true;
	}

	bool State::parseSingleDump(const SysEx& _data)
	{
		Single single;

		if(!convertTo(single, _data))
			return false;

		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		switch (buf)
		{
		case MidiBufferNum::SingleBankA:
		case MidiBufferNum::DeprecatedSingleBankA:
			if(loc >= 100)
				return false;
			m_romSingles[loc] = single;
			return true;
		case MidiBufferNum::SingleBankB:
		case MidiBufferNum::DeprecatedSingleBankB:
			if(loc >= 100)
				return false;
			m_romSingles[loc + 100] = single;
			return true;
		case MidiBufferNum::SingleBankC:
		case MidiBufferNum::DeprecatedSingleBankC:
			if(loc >= 100)
				return false;
			m_romSingles[loc + 200] = single;
			return true;
		case MidiBufferNum::SingleBankX:
		case MidiBufferNum::DeprecatedSingleBankX:
			// mQ doesn't have a card, no idea why its mentioned in the MIDI implementaiton
			return false;
		case MidiBufferNum::SingleEditBufferSingleMode:	
			m_instrumentBuffers[getGlobalParameter(GlobalParameter::InstrumentSelection)] = single;
			return true;
		case MidiBufferNum::SingleEditBufferMultiMode:
//		case MidiBufferNum::EditBufferSingleLayer:
			{
				if(getGlobalParameter(GlobalParameter::SingleMultiMode) > 0)
				{
					if(loc < m_multiSingles.size())
					{
						m_multiSingles[loc] = single;
						return true;
					}
					return false;
				}
				if(loc < m_instrumentBuffers.size())
				{
					m_instrumentBuffers[loc] = single;
					return true;
				}
			}
			return false;
		default:
			return false;
		}
	}

	bool State::parseMultiDump(const SysEx& _data)
	{
		Multi multi;

		if(!convertTo(multi, _data))
			return false;

		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		switch (buf)
		{
		case MidiBufferNum::MultiEditBuffer:
			m_multiBuffer = multi;
			return true;
		case MidiBufferNum::DeprecatedMultiBankInternal:
		case MidiBufferNum::MultiBankInternal:
			if(loc >= 100)
				return false;
			m_romMultis[loc] = multi;
			return true;
		case MidiBufferNum::DeprecatedMultiBankCard:
		case MidiBufferNum::MultiBankCard:
			// mQ doesn't have a card even though MIDI doc mentions it
			return false;
		default:
			return false;
		}
	}

	bool State::parseDrumDump(const SysEx& _data)
	{
		DrumMap drum;

		if(!convertTo(drum, _data))
			return false;

		const auto buf = static_cast<MidiBufferNum>(_data[IdxBuffer]);
		const auto loc = _data[IdxLocation];

		switch (buf)
		{
		case MidiBufferNum::DeprecatedDrumBankInternal:
		case MidiBufferNum::DrumBankInternal:
			if(loc >= 20)
				return false;
			m_romDrumMaps[loc] = drum;
			return true;
		case MidiBufferNum::DeprecatedDrumBankCard:
		case MidiBufferNum::DrumBankCard:
			return false;
		case MidiBufferNum::DrumEditBuffer:
			m_drumBuffer = drum;
			return true;
		default:
			return false;
		}
	}

	bool State::parseGlobalDump(const SysEx& _data)
	{
		return convertTo(m_global, _data);
	}

	uint8_t State::getGlobalParameter(GlobalParameter _parameter) const
	{
		return m_global[static_cast<uint32_t>(_parameter) + IdxGlobalParamFirst];
	}
}
