#include "sim.h"

#include "mc68k.h"
#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace mc68k
{
	Sim::Sim(Mc68k& _mc68k): PeripheralBase(g_simBase, g_simSize), m_mc68k(_mc68k)
	{
		write16(PeriphAddress::Picr, 0xf);
	}

	uint16_t Sim::read16(const PeriphAddress _addr)
	{
		auto r = PeripheralBase::read16(_addr);

		switch (_addr)
		{
		case PeriphAddress::Syncr:
			r |= (1<<3);	// code waits until frequency has locked in, yes it has
			return r;
		default:
			LOG("read16 addr=" << HEXN(_addr, 8));
			return r;
		}
	}

	uint8_t Sim::read8(const PeriphAddress _addr)
	{
		const auto r = PeripheralBase::read8(_addr);

		switch (_addr)
		{
		case PeriphAddress::PortE0:
		case PeriphAddress::PortE1:
			return m_portE.read();
		case PeriphAddress::PortF0:
		case PeriphAddress::PortF1:
			return m_portF.read();
		default:
			LOG("read16 addr=" << HEXN(_addr, 8));
			return r;
		}
	}

	void Sim::write8(PeriphAddress _addr, uint8_t _val)
	{
		PeripheralBase::write8(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::DdrE:
			LOG("Port E direction set to " << HEXN(_val, 2));
			m_portE.setDirection(_val);
			return;
		case PeriphAddress::DdrF:
			LOG("Port F direction set to " << HEXN(_val, 2));
			m_portF.setDirection(_val);
			return;
		case PeriphAddress::PEPar:
			LOG("Port E Pin assignment set to " << HEXN(_val, 2));
			m_portE.enablePins(~_val);
			return;
		case PeriphAddress::PFPar:
			LOG("Port F Pin assignment set to " << HEXN(_val, 2));
			m_portF.enablePins(~_val);
			return;
		case PeriphAddress::PortE0:
		case PeriphAddress::PortE1:
//			LOG("Port E write: " << HEXN(_val, 2));
			m_portE.writeTX(_val);
			return;
		case PeriphAddress::PortF0:
		case PeriphAddress::PortF1:
//			LOG("Port F write: " << HEXN(_val, 2));
			m_portF.writeTX(_val);
			return;
		case PeriphAddress::Picr:
		case PeriphAddress::Pitr:
			initTimer();
			return;
		}

		LOG("write8 addr=" << HEXN(_addr, 8) << ", val=" << HEXN(static_cast<int>(_val),2));
	}

	void Sim::write16(PeriphAddress _addr, uint16_t _val)
	{
		PeripheralBase::write16(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::Picr:
		case PeriphAddress::Pitr:
			initTimer();
			return;
		}
		LOG("write16 addr=" << HEXN(_addr, 8) << ", val=" << HEXN(_val,4));
	}

	void Sim::exec(uint32_t _deltaCycles)
	{
		if(!m_timerLoadValue)
			return;

		m_timerCurrentValue -= static_cast<int32_t>(_deltaCycles);

		if(m_timerCurrentValue < 0)
		{
			const auto picr = PeripheralBase::read16(PeriphAddress::Picr);
			const auto iv = picr & PivMask;
			const auto il = (picr & PirqlMask) >> PirqlShift;

			m_mc68k.injectInterrupt(static_cast<uint8_t>(iv), static_cast<uint8_t>(il));

			m_timerCurrentValue += m_timerLoadValue;
		}
	}

	void Sim::initTimer()
	{
		const auto picr = read16(PeriphAddress::Picr);
		const auto pitr = read16(PeriphAddress::Pitr);

		if(!(picr & PirqlMask))
		{
			m_timerLoadValue = 0;
			return;
		}

		const auto prescale = pitr & Ptp ? 512 : 1;

		// PIT Period = ((PIT Modulus)(Prescaler Value)(4)) / EXTAL Frequency
		m_timerLoadValue = (pitr & Pitm) * prescale * 4 * 16 * 20 * 2;
	}
}
