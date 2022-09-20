#include "qsm.h"

#include "logging.h"
#include "mc68k.h"

namespace mc68k
{
	constexpr uint16_t g_spcr0_mstrMask		= (1<<15);
	constexpr uint16_t g_spcr0_spbrMask		= 0xf;

	constexpr uint16_t g_spcr1_speMask		= (1<<15);

	constexpr uint16_t g_spcr2_newqpMask	= 0xf;
	constexpr uint16_t g_spcr2_endqpMask	= 0xf00;

	constexpr uint16_t g_spcr2_SpifieMask	= (1<<15);
	constexpr uint16_t g_spcr2_wrenMask		= (1<<14);
	constexpr uint16_t g_spcr2_wrtoMask		= (1<<13);

	constexpr uint16_t g_spcr3_haltMask		= (1<<8);

	constexpr uint16_t g_spsr_cptqpMask		= 0xf;
	constexpr uint16_t g_spsr_spifMask		= (1<<7);
	constexpr uint16_t g_spsr_haltAckMask	= (1<<5);

	Qsm::Qsm(Mc68k& _mc68k) : m_mc68k(_mc68k), m_qspi(*this)
	{
		write16(PeriphAddress::Spcr1, 0b0000010000000100);
		write16(PeriphAddress::Qilr,  0b0000000000001111);
		write16(PeriphAddress::SciStatus,    0b110000000);
	}

	void Qsm::write16(PeriphAddress _addr, uint16_t _val)
	{
		const auto prev = read16(_addr);

		PeripheralBase::write16(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::Spcr1:
			cancelTransmit();
			if(!(prev & g_spcr1_speMask) && (_val & g_spcr1_speMask))
				startTransmit();
			return;
		case PeriphAddress::Spcr2:
//			cancelTransmit();
			break;
		case PeriphAddress::Spcr3:
			cancelTransmit();
			// acknowledge halt if halt requested
			if(!(prev & g_spcr3_haltMask) && (_val & g_spcr3_haltMask))
				spsr(spsr() | g_spsr_haltAckMask);
			break;
		case PeriphAddress::Pqspar:
			LOG("Set PQSPAR to " << HEXN(_val, 4));
			m_portQS.enablePins(~(_val >> 8));
			m_portQS.setDirection(_val & 0xff);
			return;
		case PeriphAddress::SciControl0:
			LOG("Set SCCR0 to " << HEXN(_val, 4));
			return;
		case PeriphAddress::SciControl1:
//			LOG("Set SCCR1 to " << HEXN(_val, 4));
			if(bitTest(_val, Sccr1Bits::TransmitInterruptEnable) && !bitTest(prev, Sccr1Bits::TransmitInterruptEnable))
				injectInterrupt(ScsrBits::TransmitDataRegisterEmpty);
			return;
		case PeriphAddress::SciStatus:
			LOG("Set SCSR to " << HEXN(_val, 4));
			return;
		case PeriphAddress::SciData:
			writeSciData(_val);
			return;
		}
//		LOG("write16 addr=" << HEXN(_addr, 8) << ", val=" << HEXN(_val,4));
	}

	uint16_t Qsm::read16(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::SciStatus:
			return readSciStatus();
		case PeriphAddress::SciData:
			return readSciRX();
		case PeriphAddress::Portqs:
			return m_portQS.read();
		case PeriphAddress::SciControl0:
		case PeriphAddress::SciControl1:
			return PeripheralBase::read16(_addr);
		}

//		LOG("read16 addr=" << HEXN(_addr, 8));
		return PeripheralBase::read16(_addr);
	}

	void Qsm::write8(PeriphAddress _addr, uint8_t _val)
	{
		if(_addr == PeriphAddress::SciControl1LSB)
		{
			write16(PeriphAddress::SciControl1, (read16(PeriphAddress::SciControl1) & 0xff00) | _val);
			return;
		}

		const auto prev = read16(_addr);

		PeripheralBase::write8(_addr, _val);

		const auto newVal = read16(_addr);

		switch (_addr)
		{
		case PeriphAddress::Spcr1:
			if(!(prev & g_spcr1_speMask) && (newVal & g_spcr1_speMask))
				startTransmit();
			return;
		case PeriphAddress::Spcr2:
//			cancelTransmit();
			return;
		case PeriphAddress::Spcr3:
			cancelTransmit();
			// acknowledge halt if halt requested
			if(!(prev & g_spcr3_haltMask) && (newVal & g_spcr3_haltMask))
				spsr(spsr() | g_spsr_haltAckMask);
			return;
		case PeriphAddress::Ddrqs:
			LOG("Set DDRQS to " << HEXN(_val, 2));
			m_portQS.setDirection(_val);
			return;
		case PeriphAddress::Pqspar:
			LOG("Set PQSPAR to " << HEXN(_val, 2));
			m_portQS.enablePins(~_val);
			return;
		case PeriphAddress::Portqs:
			LOG("Set PortQS to " << HEXN(_val,2));
			m_portQS.writeTX(_val);
			return;
		case PeriphAddress::SciData:
			writeSciData(_val);
			return;
		case PeriphAddress::SciStatus:
			LOG("Set SCSR to " << HEXN(_val, 2));
			return;
		}
//		LOG("write8 addr=" << HEXN(_addr, 8) << ", val=" << HEXN(static_cast<int>(_val),2));
	}

	uint8_t Qsm::read8(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::Portqs:
			return m_portQS.read();
		case PeriphAddress::SciStatus:
			return readSciStatus() >> 8;
		case PeriphAddress::SciData:
			return read16(PeriphAddress::SciData) >> 8;
		case PeriphAddress::SciDataLSB:
			return read16(PeriphAddress::SciData) & 0xff;
		case PeriphAddress::SciControl1LSB:
		case PeriphAddress::Qilr:
		case PeriphAddress::Qivr:
		case PeriphAddress::Spsr:
			return PeripheralBase::read8(_addr);
		}
//		LOG("read8 addr=" << HEXN(_addr, 8));
		return PeripheralBase::read8(_addr);
	}

	void Qsm::injectInterrupt(ScsrBits)
	{
		const auto vector = PeripheralBase::read8(PeriphAddress::Qivr) & 0xfe;
		const auto levelQsci = static_cast<uint8_t>(PeripheralBase::read8(PeriphAddress::Qilr) & 0x7);
		m_mc68k.injectInterrupt(vector, levelQsci);
	}

	void Qsm::exec(uint32_t _deltaCycles)
	{
		PeripheralBase::exec(_deltaCycles);

		m_qspi.exec();

		if(m_nextQueue != 0xff)
		{
			if(m_spiDelay > 0)
				--m_spiDelay;
			else
				execTransmit();
		}

		if(m_pendingTxDataCounter == 2)
		{
			--m_pendingTxDataCounter;
			set(ScsrBits::TransmitDataRegisterEmpty);

			if(bitTest(Sccr1Bits::TransmitInterruptEnable))
				injectInterrupt(ScsrBits::TransmitDataRegisterEmpty);
		}
		else if(m_pendingTxDataCounter == 1)
		{
			--m_pendingTxDataCounter;

			set(ScsrBits::TransmitComplete);

			if(bitTest(Sccr1Bits::TransmitCompleteInterruptEnable))
				injectInterrupt(ScsrBits::TransmitComplete);
		}

		// SCI
		if(m_sciRxData.empty())
			return;

		if(!bitTest(Sccr1Bits::ReceiverEnable))
		{
//			LOG("Discarding SCI data " << HEXN(m_sciRxData.front(), 4) << ", receiver not enabled");
//			m_sciRxData.pop_front();
			return;
		}

		if(!bitTest(ScsrBits::ReceiveDataRegisterFull))
		{
			set(ScsrBits::ReceiveDataRegisterFull);

			if(bitTest(Sccr1Bits::ReceiverInterruptEnable))
				injectInterrupt(ScsrBits::ReceiveDataRegisterFull);
		}
	}

	void Qsm::writeSciRX(uint16_t _data)
	{
		m_sciRxData.push_back(_data);
	}

	void Qsm::readSciTX(std::deque<uint16_t>& _dst)
	{
		m_sciTxData.swap(_dst);
		m_sciTxData.clear();
	}

	void Qsm::startTransmit(const bool _startAtZero/* = false*/)
	{
		// are we master?
		if(!(spcr0() & g_spcr0_mstrMask))
			return;

		const auto cr3 = spcr3();

		const auto halt = cr3 & g_spcr3_haltMask;

		if(halt)
			return;

		m_nextQueue = _startAtZero ? 0 : (spcr2() & g_spcr2_newqpMask);
	}

	void Qsm::execTransmit()
	{
		const auto cr3 = spcr3();

		const auto halt = cr3 & g_spcr3_haltMask;

		if(halt)
			return;

		// "SPI Baud Rate = System Clock / (2*SPBR)"
		const auto br = spcr0() & g_spcr0_spbrMask;
		m_spiDelay = br << 1;

		// push out data
		const auto data = PeripheralBase::read16(transmitRamAddr(m_nextQueue));
		m_spiTxData.push_back(data);

		// update completed queue index
		auto sr = spsr();
		sr &= ~g_spsr_cptqpMask;
		sr |= m_nextQueue;

		spsr(sr);

		// advance to next or end
		++m_nextQueue;

		const auto endqp = (spcr2() & g_spcr2_endqpMask) >> 8;
		const auto finished = m_nextQueue > endqp;

		if(finished)
		{
			finishTransfer();
		}
	}

	void Qsm::cancelTransmit()
	{
		m_nextQueue = 0xff;

		// set completion flag
		spsr(spsr() | g_spsr_spifMask);

		// clear enabled flag
		auto cr1 = spcr1();
		cr1 &= ~g_spcr1_speMask;
		spcr1(cr1);
	}

	uint16_t Qsm::bitTest(uint16_t _value, Sccr1Bits _bit)
	{
		return _value & (1<<static_cast<uint32_t>(_bit));
	}

	uint16_t Qsm::bitTest(Sccr1Bits _bit)
	{
		return bitTest(read16(PeriphAddress::SciControl1), _bit);
	}

	uint16_t Qsm::bitTest(ScsrBits _bit)
	{
		return read16(PeriphAddress::SciStatus) & (1<<static_cast<uint32_t>(_bit));
	}

	void Qsm::clear(ScsrBits _bit)
	{
		auto v = PeripheralBase::read16(PeriphAddress::SciStatus);
		v &= ~(1<<static_cast<uint32_t>(_bit));
		PeripheralBase::write16(PeriphAddress::SciStatus, v);
	}

	void Qsm::set(ScsrBits _bit)
	{
		auto v = PeripheralBase::read16(PeriphAddress::SciStatus);
		v |= (1<<static_cast<uint32_t>(_bit));
		PeripheralBase::write16(PeriphAddress::SciStatus, v);
	}

	uint16_t Qsm::readSciRX()
	{
		if(m_sciRxData.empty())
		{
//			LOG("Empty SCI read");
			return 0;
		}

		clear(ScsrBits::ReceiveDataRegisterFull);
		const auto res = m_sciRxData.front();
		m_sciRxData.pop_front();
		return res;
	}

	void Qsm::finishTransfer()
	{
		m_nextQueue = 0xff;

		// set completion flag
		spsr(spsr() | g_spsr_spifMask);

		const auto cr2 = spcr2();
		const auto cr3 = spcr3();

		const auto wrap = cr2 & g_spcr2_wrenMask;
		const auto halt = cr3 & g_spcr3_haltMask;

		if(!wrap || halt)
		{
			// clear enabled flag
			auto cr1 = spcr1();
			cr1 &= ~g_spcr1_speMask;
			spcr1(cr1);
		}

		if(cr2 & g_spcr2_SpifieMask)
		{
			// fire interrupt
			const auto vector = PeripheralBase::read8(PeriphAddress::Qivr) | 1;
			const auto levelQspi = static_cast<uint8_t>((PeripheralBase::read8(PeriphAddress::Qilr) >> 3) & 0x7);
			if(!m_mc68k.hasPendingInterrupt(vector, levelQspi))
				m_mc68k.injectInterrupt(vector, levelQspi);
		}

		if(wrap && !halt)
		{
			const auto wrapToZero = !(cr2 & g_spcr2_wrtoMask);
			startTransmit(wrapToZero);
		}

		if(halt)
		{
			// acknowledge halt
			spsr(spsr() | g_spsr_haltAckMask);
		}
	}

	PeriphAddress Qsm::transmitRamAddr(uint8_t _offset)
	{
		return static_cast<PeriphAddress>(static_cast<uint32_t>(PeriphAddress::TransmitRam0) + (_offset<<1));
	}

	void Qsm::writeSciData(const uint16_t _data)
	{
		if(!bitTest(Sccr1Bits::TransmitterEnable))
			return;

		m_sciTxData.push_back(_data);
		m_pendingTxDataCounter = 2;
	}

	uint16_t Qsm::readSciStatus()
	{
		// we're always done with everything
		set(ScsrBits::TransmitDataRegisterEmpty);
		set(ScsrBits::TransmitComplete);
		const auto r = PeripheralBase::read16(PeriphAddress::SciStatus);
//		LOG("Read SCSR, res=" << HEXN(r, 4));
		return r;
	}
}
