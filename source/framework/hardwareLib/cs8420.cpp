#include "cs8420.h"

namespace hwLib
{
	Cs8420::Cs8420()
	{
		reset();
	}

	void Cs8420::reset()
	{
		m_regs.fill(0);
		m_regs[IdAndVersion] = 0x10;  // CS8420, revision 0
		m_map = 0;
		m_increment = false;
		m_selected = false;
		m_clock = false;
		m_dataOut = false;
		m_phase = Phase::Address;
		m_shiftIn = 0;
		m_bitCount = 0;
		m_readBit = 0;
	}

	void Cs8420::setSelect(const bool _selected)
	{
		if(_selected == m_selected) return;
		m_selected = _selected;
		m_phase = Phase::Address;
		m_shiftIn = 0;
		m_bitCount = 0;
		m_readBit = 0;
		m_dataOut = false;
	}

	void Cs8420::setClock(const bool _high, const bool _dataIn)
	{
		if(_high == m_clock) return;
		m_clock = _high;
		if(!m_selected) return;
		if(_high)
		{
			m_shiftIn = static_cast<uint8_t>((m_shiftIn << 1) | (_dataIn ? 1 : 0));
			if(++m_bitCount < 8) return;
			m_bitCount = 0;
			byteReceived(m_shiftIn);
			return;
		}
		if(m_phase != Phase::Read) return;
		// Read data leaves MSB first, one bit per falling edge, the MAP moving
		// on after each byte when auto increment is on.
		m_dataOut = ((readReg(m_map) >> (7 - m_readBit)) & 1) != 0;
		if(++m_readBit < 8) return;
		m_readBit = 0;
		if(m_increment) m_map = static_cast<uint8_t>((m_map + 1) & 0x7f);
	}

	void Cs8420::byteReceived(const uint8_t _value)
	{
		switch(m_phase)
		{
		case Phase::Address:
			if((_value >> 1) != ChipAddress)
			{
				m_phase = Phase::Ignored;
				return;
			}
			m_phase = (_value & 1) ? Phase::Read : Phase::Map;
			m_readBit = 0;
			return;
		case Phase::Map:
			m_map = _value & 0x7f;
			m_increment = (_value & 0x80) != 0;
			m_phase = Phase::Write;
			return;
		case Phase::Write:
			writeReg(m_map, _value);
			if(m_increment) m_map = static_cast<uint8_t>((m_map + 1) & 0x7f);
			return;
		default:
			return;
		}
	}

	uint8_t Cs8420::readReg(const uint8_t _index) const
	{
		switch(_index)
		{
		case ReceiverError:
			// Errors are reported as far as their mask bits allow.
			return static_cast<uint8_t>((m_locked ? 0 : ErrorUnlock) & m_regs[ReceiverErrorMask]);
		case ReceiverChannelStatus:
			return 0;
		default:
			return m_regs[_index & 0x7f];
		}
	}

	void Cs8420::writeReg(const uint8_t _index, const uint8_t _value)
	{
		++m_writes;
		switch(_index)
		{
		case Interrupt1Status:
		case Interrupt2Status:
		case ReceiverChannelStatus:
		case ReceiverError:
		case IdAndVersion:
			return;  // read only
		default:
			m_regs[_index & 0x7f] = _value;
			return;
		}
	}
}
