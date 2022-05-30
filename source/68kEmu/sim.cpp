#include "sim.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

namespace mc68k
{
	uint16_t Sim::read16(const PeriphAddress _addr)
	{
		auto r = PeripheralBase::read16(_addr);

		switch (_addr)
		{
		case PeriphAddress::Syncr:
			r |= (1<<3);	// code waits until frequency has locked in, yes it has
			return r;
		default:
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
			break;
		case PeriphAddress::DdrF:
			LOG("Port F direction set to " << HEXN(_val, 2));
			m_portF.setDirection(_val);
			break;
		case PeriphAddress::PEPar:
			LOG("Port E Pin assignment set to " << HEXN(_val, 2));
			m_portE.enablePins(~_val);
			break;
		case PeriphAddress::PFPar:
			LOG("Port F Pin assignment set to " << HEXN(_val, 2));
			m_portF.enablePins(~_val);
			break;
		case PeriphAddress::PortE0:
		case PeriphAddress::PortE1:
//			LOG("Port E write: " << HEXN(_val, 2));
			m_portE.writeTX(_val);
			break;
		case PeriphAddress::PortF0:
		case PeriphAddress::PortF1:
//			LOG("Port F write: " << HEXN(_val, 2));
			m_portF.writeTX(_val);
			break;
		}
	}
}
