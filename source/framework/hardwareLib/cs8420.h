#pragma once

#include <array>
#include <cstdint>

namespace hwLib
{
	// Cirrus Logic CS8420 digital audio transceiver, control port side.
	//
	// The host drives it as a three-wire SPI slave: /CS low, a chip address
	// byte (20 = write, 21 = read), for a write the MAP byte (bit 7 = auto
	// increment, bits 6-0 the register address) followed by data bytes, for
	// a read data bytes from the current MAP.  Data into the chip is sampled
	// on the rising clock edge, data out changes on the falling edge.
	//
	// The audio side is not modelled.  With nothing on the receiver the
	// UNLOCK error is permanently set and reported through the Receiver
	// Error register as far as the Receiver Error Mask allows; the received
	// channel status reads as zero.
	class Cs8420
	{
	public:
		enum Register : uint8_t
		{
			MiscControl1 = 0x01,
			MiscControl2 = 0x02,
			DataFlowControl = 0x03,
			ClockSourceControl = 0x04,
			SerialInputFormat = 0x05,
			SerialOutputFormat = 0x06,
			Interrupt1Status = 0x07,
			Interrupt2Status = 0x08,
			Interrupt1Mask = 0x09,
			Interrupt2Mask = 0x0c,
			ReceiverChannelStatus = 0x0f,
			ReceiverError = 0x10,
			ReceiverErrorMask = 0x11,
			CsDataBufferControl = 0x12,
			UDataBufferControl = 0x13,
			CBitBuffer = 0x20,
			IdAndVersion = 0x7f
		};
		static constexpr uint8_t ErrorUnlock = 0x10;
		static constexpr uint8_t ClockRun = 0x40;
		static constexpr uint8_t ChipAddress = 0x10;  // the seven address bits above R/W

		Cs8420();
		void reset();

		// control port pins
		void setSelect(bool _selected);           // /CS, true = low
		void setClock(bool _high, bool _dataIn);  // CCLK with the CDIN level
		bool dataOut() const { return m_dataOut; }

		uint8_t reg(const uint8_t _index) const { return m_regs[_index & 0x7f]; }
		bool isRunning() const { return (m_regs[ClockSourceControl] & ClockRun) != 0; }
		uint64_t writes() const { return m_writes; }

		// whether the receiver PLL sees a source
		void setLocked(const bool _locked) { m_locked = _locked; }
		bool isLocked() const { return m_locked; }

	private:
		enum class Phase
		{
			Address,
			Map,
			Write,
			Read,
			Ignored
		};

		uint8_t readReg(uint8_t _index) const;
		void writeReg(uint8_t _index, uint8_t _value);
		void byteReceived(uint8_t _value);

		std::array<uint8_t, 0x80> m_regs{};
		uint8_t m_map = 0;
		bool m_increment = false;
		bool m_locked = false;
		bool m_selected = false;
		bool m_clock = false;
		bool m_dataOut = false;
		Phase m_phase = Phase::Address;
		uint8_t m_shiftIn = 0;
		unsigned m_bitCount = 0;   // bits received of the current byte
		unsigned m_readBit = 0;    // bits presented of the current read byte
		uint64_t m_writes = 0;
	};
}
