#include "i2c.h"

#include <cassert>

#include "dsp56kEmu/logging.h"

namespace hwLib
{
	void I2c::masterWrite(const bool _sda, const bool _scl)
	{
		if(_sda != m_sda && _scl != m_scl)
		{
			assert(false && "only one pin should flip");
			return;
		}

		if(_sda != m_sda)
		{
			m_sda = _sda;
			sdaFlip(_sda);
		}
		else if(_scl != m_scl)
		{
			m_scl = _scl;
			sclFlip(_scl);
		}
	}

	std::optional<bool> I2c::masterRead(const bool _scl)
	{
		if(_scl == m_scl)
			return {};

		if(m_state != State::Start)
			return {};

		m_scl = _scl;

		if(_scl)
		{
			if(m_nextBit == BitAck)
			{
				m_nextBit = Bit7;
				return m_ackBit;	// this was returned already in onAck()
			}

			if(m_nextBit >= Bit0 && m_nextBit <= Bit7)
			{
				if(m_nextBit == Bit7)
					m_byte = onReadByte();

				auto res = m_byte & (1<<m_nextBit);
				--m_nextBit;
				return res;
			}
		}

		return {};
	}

	std::optional<bool> I2c::setSdaWrite(const bool _write)
	{
		if(m_sdaWrite == _write)
			return {};

		m_sdaWrite = _write;

		if(m_state != State::Start)
			return {};

		if(!m_sdaWrite)
		{
			if(m_nextBit == BitAck)
			{
				const auto ackBit = onAck();
				if(ackBit)
				{
					m_ackBit = *ackBit;
				}
				return ackBit;
			}
		}
		return {};
	}

	void I2c::onStateChanged(const State _state)
	{
		LOG("state: " << (_state == State::Start ? "start" : "stop"));

		switch (_state)
		{
		case State::Stop:
			m_nextBit = BitInvalid;
			break;
		case State::Start:
			m_nextBit = Bit7;
			m_byte = 0;
			break;
		}
	}

	void I2c::onStartCondition()
	{
		setState(State::Start);
	}

	void I2c::onStopCondition()
	{
		setState(State::Stop);
	}

	void I2c::onByteWritten()
	{
		LOG("Got byte " << HEXN(byte(), 2));
	}

	void I2c::sdaFlip(const bool _sda)
	{
		if(m_scl)
		{
			if(!_sda)
				onStartCondition();
			else
				onStopCondition();
		}
	}

	void I2c::sclFlip(const bool _scl)
	{
		if(_scl && m_state == State::Start)
		{
			if(m_nextBit >= Bit0 && m_nextBit <= Bit7)
			{
				LOG("next bit " << static_cast<int>(m_nextBit) << " = " << m_sda);

				if(m_nextBit == Bit7)
					m_byte = 0;

				// data input
				if(m_sda)
					m_byte |= (1<<m_nextBit);

				--m_nextBit;

				if(m_nextBit < 0)
				{
					onByteWritten();
				}
			}
			else if(m_nextBit == BitAck)
			{
				LOG("ACK by master=" << m_sda);
				m_nextBit = 7;
			}
		}
	}

	void I2c::setState(const State _state)
	{
/*		if(m_state == _state)
			return;
*/		m_state = _state;
		onStateChanged(_state);
	}
}
