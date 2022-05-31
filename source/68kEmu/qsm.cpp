#include "qsm.h"

#include "mc68k.h"

#include "dsp56kEmu/logging.h"

namespace mc68k
{
	constexpr uint16_t g_spcr1_speMask		= (1<<15);
	constexpr uint16_t g_spcr0_mstrMask		= (1<<15);
	constexpr uint16_t g_spcr2_inqpMask		= 0xf;
	constexpr uint16_t g_spcr2_SpifieMask	= (1<<15);
	constexpr uint16_t g_spsr_cptqpMask		= 0xf;
	constexpr uint16_t g_spsr_spifMask		= (1<<7);

	Qsm::Qsm(Mc68k& _mc68k) : PeripheralBase(g_qsmBase, g_qsmSize), m_mc68k(_mc68k), m_qspi(*this)
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
			if(!(prev & g_spcr1_speMask) && (_val & g_spcr1_speMask))
				startTransmit();
			break;
		case PeriphAddress::Pqspar:
			LOG("Set PQSPAR to " << HEXN(_val, 4));
			m_portQS.enablePins(~(_val >> 8));
			m_portQS.setDirection(_val & 0xff);
			break;
		case PeriphAddress::SciControl0:
			LOG("Set SCCR0 to " << HEXN(_val, 4));
			break;
		case PeriphAddress::SciControl1:
			LOG("Set SCCR1 to " << HEXN(_val, 4));
			if(bitTest(_val, Sccr1Bits::TransmitInterruptEnable) && !bitTest(prev, Sccr1Bits::TransmitInterruptEnable))
				injectInterrupt(ScsrBits::TransmitDataRegisterEmpty);
			break;
		case PeriphAddress::SciStatus:
			LOG("Set SCSR to " << HEXN(_val, 4));
			break;
		case PeriphAddress::SciData:
			writeSciData(_val);
			break;
		}
	}

	uint16_t Qsm::read16(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::SciStatus:
			return readSciStatus();
		}
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
			break;
		case PeriphAddress::Ddrqs:
			LOG("Set DDRQS to " << HEXN(_val, 2));
			m_portQS.setDirection(_val);
			break;
		case PeriphAddress::Pqspar:
			LOG("Set PQSPAR to " << HEXN(_val, 2));
			m_portQS.enablePins(~_val);
			break;
		case PeriphAddress::SciData:
			writeSciData(_val);
			break;
		case PeriphAddress::SciStatus:
			LOG("Set SCSR to " << HEXN(_val, 2));
			break;
		}
	}

	uint8_t Qsm::read8(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::Portqs:
			{
				const auto res = PeripheralBase::read8(_addr);
				// set QS3 to high as the code is waiting for a high, connected to DSP reset line, indicates that DSP is alive
				return res | (1<<3);
			}
		case PeriphAddress::SciStatus:
			return readSciStatus() >> 8;
		}
		return PeripheralBase::read8(_addr);
	}

	void Qsm::injectInterrupt(ScsrBits)
	{
		const auto vector = read8(PeriphAddress::Qivr);
		const auto levelQsci = static_cast<uint8_t>(read8(PeriphAddress::Qilr) & 0x7);
		m_mc68k.injectInterrupt(vector, levelQsci);
	}

	void Qsm::exec(uint32_t _deltaCycles)
	{
		PeripheralBase::exec(_deltaCycles);

		m_qspi.exec();

		if(m_nextQueue != 0xff)
		{
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

	void Qsm::startTransmit()
	{
		// are we master?
		if(!(spcr0() & g_spcr0_mstrMask))
			return;

		m_nextQueue = spcr2() & g_spcr2_inqpMask;
	}

	void Qsm::execTransmit()
	{
		// push out data
		const auto data = read16(transmitRamAddr(m_nextQueue));
		m_spiTxData.push_back(data);

		// update completed queue index
		auto sr = spsr();
		sr &= ~g_spsr_cptqpMask;
		sr |= m_nextQueue;

		spsr(sr);

		// advance to next or end
		++m_nextQueue;

		const auto finished = m_nextQueue >= 16;

		if(finished)
		{
			finishTransfer();
		}
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

	void Qsm::finishTransfer()
	{
		m_nextQueue = 0xff;

		// set completion flag
		spsr(spsr() | g_spsr_spifMask);

		// clear enabled flag
		auto cr1 = spcr1();
		cr1 &= ~g_spcr1_speMask;
		spcr1(cr1);

		const auto cr2 = spcr2();

		if(cr2 & g_spcr2_SpifieMask)
		{
			// fire interrupt
			const auto vector = read8(PeriphAddress::Qivr);
			const auto levelQspi = static_cast<uint8_t>((read8(PeriphAddress::Qilr) >> 3) & 0x7);
			m_mc68k.injectInterrupt(vector, levelQspi);
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
		LOG("Read SCSR, res=" << HEXN(r, 4));
		return r;
	}
}
