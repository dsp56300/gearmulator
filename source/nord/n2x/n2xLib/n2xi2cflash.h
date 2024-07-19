#pragma once

#include <optional>

#include "i2c.h"
#include "n2xromdata.h"
#include "n2xtypes.h"

namespace n2x
{
	class I2cFlash : public I2c, public RomData<g_flashSize>
	{
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

	protected:
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
	};
}
