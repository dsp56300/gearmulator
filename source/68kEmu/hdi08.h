#pragma once

#include <array>
#include <deque>
#include <functional>

#include "peripheralBase.h"

namespace mc68k
{
	class Hdi08 final : public PeripheralBase<g_hdi08Base, g_hdi08Size>
	{
	public:
		enum IsrBits
		{
			Rxdf			= 1<<0,		// ISR Receive Data Register Full (RXDF) Bit 0
			Txde			= 1<<1,		// ISR Transmit Data Register Empty (TXDE) Bit 1
			Trdy			= 1<<2,		// ISR Transmitter Ready (TRDY) Bit 2
			Hf2				= 1<<3,		// ISR Host Flag 2 (HF2) Bit 3
			Hf3				= 1<<4,		// ISR Host Flag 3 (HF3) Bit 4
			IsrReserved5	= 1<<5,		// ISR Reserved Bits 5-6
			IsrReverved6	= 1<<6,		// ISR Reserved Bits 5-6
			Hreq			= 1<<7,		// ISR Host Request (HREQ) Bit 7
		};

		enum IcrBits
		{
			Rreq			= 1<<0,		// ICR Receive Request Enable (RREQ) Bit 0
			Treq			= 1<<1,		// ICR Transmit Request Enable (TREQ) Bit 1
			Hdrq			= 1<<2,		// ICR Double Host Request (HDRQ) Bit 2
			Hf0				= 1<<3,		// ICR Host Flag 0 (HF0) Bit 3
			Hf1				= 1<<4,		// ICR Host Flag 1 (HF1) Bit 4
			Hlend			= 1<<5,		// ICR Host Little Endian (HLEND) Bit 5
			Hm0				= Hlend,
			Hm1				= 1<<6,		// ICR Host Mode Control (HM1 and HM0 bits) Bits 5-6
			Init			= 1<<7,		// ICR Initialize Bit (INIT) Bit 7
		};

		enum CvrBits
		{
			Hv				= 0x7f,		// CVR Host Vector (HV[6:0]) Bits 0–6, Interrupt Vector = 2*Hv
			Hc				= (1<<7),	// CVR Host Command Bit (HC) Bit 7
		};

		using CallbackRxEmpty = std::function<void(bool)>;
		using CallbackWriteTx = std::function<void(uint32_t)>;
		using CallbackWriteIrq = std::function<void(uint8_t)>;
		using CallbackReadIsr = std::function<uint8_t(uint8_t)>;

		Hdi08();

		uint8_t read8(PeriphAddress _addr) override;
		uint16_t read16(PeriphAddress _addr) override;
		void write8(PeriphAddress _addr, uint8_t _val) override;
		void write16(PeriphAddress _addr, uint16_t _val) override;

		void pollTx(std::deque<uint32_t>& _dst)
		{
			std::swap(_dst, m_txData);
		}

		bool pollInterruptRequest(uint8_t& _addr);

		void writeRx(uint32_t _word);

		void exec(uint32_t _deltaCycles) override;

		uint8_t isr()
		{
			auto isr = m_readIsrCallback(PeripheralBase::read8(PeriphAddress::HdiISR));

			// we want new data for transmission
			isr |= Txde;

			return isr;
		}

		uint8_t icr()
		{
			return PeripheralBase::read8(PeriphAddress::HdiICR);
		}

		void isr(uint8_t _isr) { write8(PeriphAddress::HdiISR, _isr); }
		void icr(uint8_t _icr) { write8(PeriphAddress::HdiICR, _icr); }

		bool canReceiveData();

		void setRxEmptyCallback(const CallbackRxEmpty& _rxEmptyCallback)
		{
			m_rxEmptyCallback = _rxEmptyCallback;
		}
		void setWriteTxCallback(const CallbackWriteTx& _writeTxCallback);
		void setWriteIrqCallback(const CallbackWriteIrq& _writeIrqCallback);
		void setReadIsrCallback(const CallbackReadIsr& _readIsrCallback);
	private:
		enum class WordFlags
		{
			None = 0,

			H = 0,
			M = 1,
			L = 2,

			HMask = (1<<H),
			MMask = (1<<M),
			LMask = (1<<L),

			Mask = (HMask | MMask | LMask)
		};

		void writeTX(WordFlags _index, uint8_t _val);
		static WordFlags makeMask(WordFlags _index);
		static void addIndex(WordFlags& _existing, WordFlags _indexToAdd);
		static void removeIndex(WordFlags& _existing, WordFlags _indexToRemove);
		uint8_t littleEndian();
		uint8_t readRX(WordFlags _index);
		bool pollRx();

		WordFlags m_writtenFlags = WordFlags::None;
		WordFlags m_readFlags = WordFlags::None;

		std::array<uint8_t, 3> m_txBytes{};

		std::deque<uint32_t> m_txData;
		std::deque<uint32_t> m_rxData;
		uint32_t m_rxd = 0;
		std::deque<uint8_t> m_pendingInterruptRequests;
		uint32_t m_readTimeoutCycles = 0;

		CallbackRxEmpty m_rxEmptyCallback;
		CallbackWriteTx m_writeTxCallback;
		CallbackWriteIrq m_writeIrqCallback;
		CallbackReadIsr m_readIsrCallback;
	};
}
