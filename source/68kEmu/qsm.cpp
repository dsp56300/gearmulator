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
			break;
		}
	}

	uint16_t Qsm::read16(PeriphAddress _addr)
	{
		return PeripheralBase::read16(_addr);
	}

	void Qsm::write8(PeriphAddress _addr, uint8_t _val)
	{
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
			break;
		case PeriphAddress::Pqspar:
			LOG("Set PQSPAR to " << HEXN(_val, 2));
			break;
		}
	}

	uint8_t Qsm::read8(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::Portqs:
			{
				const auto pqspar = read16(PeriphAddress::Pqspar);
				const auto ddrqs = read8(PeriphAddress::Ddrqs);

				// set all inputs to high as the code is waiting for a high
				const auto gpios = ~(pqspar>>8);
				const auto inputs = ~(ddrqs);

				const auto toBeSet = (gpios & inputs) & 0xff;
				const auto res = PeripheralBase::read8(_addr);
				return res | toBeSet;
			}
		}
		return PeripheralBase::read8(_addr);
	}

	void Qsm::exec()
	{
		PeripheralBase::exec();
		m_qspi.exec();

		if(m_nextQueue != 0xff)
		{
			execTransmit();
		}
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
		m_txData.push_back(data);

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
}
