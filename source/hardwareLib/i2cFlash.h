#pragma once

#include <array>
#include <optional>
#include <string>

#include "i2c.h"

namespace hwLib
{
	// M24512 64kx 8 i2c flash chip emulation
	class I2cFlash : public I2c
	{
	public:
		using Data = std::array<uint8_t, 0x10000>;

		I2cFlash()
		{
			m_data.fill(0xff);
		}

		explicit I2cFlash(const Data& _data) : m_data(_data)
		{
		}

		void saveAs(const std::string& _filename) const;

	private:
		enum class State
		{
			ReadDeviceSelect,	AckDeviceSelect,
			ReadAddressMSB,		AckAddressMSB,
			ReadAddressLSB,		AckAddressLSB,
			ReadWriteData,
		};

		enum DeviceSelectMask
		{
			Area       = 0b1111'000'0,
			ChipEnable = 0b0000'111'0,
			Rw         = 0b0000'000'1,
		};

		enum DeviceSelectValues
		{
			AreaMemory = 0b1010'000'0,
			AreaId     = 0b1011'000'0,
			Read       = 0b0000'000'1,
			Write      = 0b0000'000'0,
		};

		void onStartCondition() override;
		void onStopCondition() override;
		void onByteWritten() override;
		std::optional<bool> onAck() override;
		uint8_t onReadByte() override;
		void writeByte(uint8_t _byte);
		void advanceAddress();

		State m_state = State::ReadDeviceSelect;
		uint8_t m_deviceSelect = 0;
		uint64_t m_address = 0;

		Data m_data;
	};
}
