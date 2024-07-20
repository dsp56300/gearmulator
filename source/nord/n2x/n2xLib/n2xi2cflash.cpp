#include "n2xi2cflash.h"

#include <cassert>

#include "dsp56kEmu/logging.h"

namespace n2x
{
	void I2cFlash::onStartCondition()
	{
		m_state = State::ReadDeviceSelect;
		I2c::onStartCondition();
	}

	void I2cFlash::onStopCondition()
	{
		I2c::onStopCondition();
	}

	void I2cFlash::onByteWritten()
	{
		I2c::onByteWritten();

		switch (m_state)
		{
		case State::ReadDeviceSelect:
			m_deviceSelect = byte();
			m_state = State::AckDeviceSelect;
			break;
		case State::ReadAddressMSB:
			m_address = byte() << 8;
			m_state = State::AckAddressMSB;
			break;
		case State::ReadAddressLSB:
			m_address |= byte();
			m_state = State::AckAddressLSB;
			break;
		case State::ReadWriteData:
			writeByte(byte());
			break;
		default:
			assert(false && "invalid state");
			break;
		}
	}

	std::optional<bool> I2cFlash::onAck()
	{
		switch (m_state)
		{
		case State::AckDeviceSelect:
			m_state = State::ReadAddressMSB;
			return false;
		case State::AckAddressMSB:
			m_state = State::ReadAddressLSB;
			return false;
		case State::AckAddressLSB:
			m_state = State::ReadWriteData;
			return false;
		case State::ReadWriteData:
			return false;
		default:
			assert(false && "invalid state");
			return true;
		}
	}

	uint8_t I2cFlash::onReadByte()
	{
		assert((m_deviceSelect & DeviceSelectMask::Area) == DeviceSelectValues::AreaMemory);
		assert((m_deviceSelect & DeviceSelectMask::Rw) == DeviceSelectValues::Read);
		const auto res = data()[m_address];
		advanceAddress();
		return res;
	}

	void I2cFlash::writeByte(uint8_t _byte)
	{
		assert((m_deviceSelect & DeviceSelectMask::Area) == DeviceSelectValues::AreaMemory);
		assert((m_deviceSelect & DeviceSelectMask::Rw) == DeviceSelectValues::Write);

		data()[m_address] = _byte;
		advanceAddress();
	}

	void I2cFlash::advanceAddress()
	{
		m_address = (m_address & 0xFF80) | ((m_address + 1) & 0x7F);
	}
}