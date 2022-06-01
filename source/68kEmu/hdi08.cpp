#include "hdi08.h"

#include <cassert>

#include "dsp56kEmu/logging.h"

namespace mc68k
{
	Hdi08::Hdi08(): PeripheralBase(g_hdi08Base, g_hdi08Size)
	{
		write8(PeriphAddress::HdiIVR, 0xf);
	}

	uint8_t Hdi08::read8(PeriphAddress _addr)
	{
		auto r = PeripheralBase::read8(_addr);

		switch (_addr)
		{
		case PeriphAddress::HdiISR:
			// we want new data for transmission
			r |= Txde;
			return r;
		case PeriphAddress::HdiTXH:	return readRX(WordFlags::H);
		case PeriphAddress::HdiTXM:	return readRX(WordFlags::M);
		case PeriphAddress::HdiTXL:	return readRX(WordFlags::L);
		}
		return r;
	}

	uint16_t Hdi08::read16(PeriphAddress _addr)
	{
		return PeripheralBase::read16(_addr);
	}

	void Hdi08::write8(PeriphAddress _addr, uint8_t _val)
	{
		PeripheralBase::write8(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::HdiICR:
			if(_val & Init)
			{
				LOG("HDI08 Initialization, HREQ=" << (_val & Rreq) << ", TREQ=" << (_val & Treq));
			}
			break;
		case PeriphAddress::HdiCVR:
			if(_val & Hc)
			{
				const auto addr = static_cast<uint8_t>((_val & Hv) << 1);
//				LOG("HDI08 Host Vector Interrupt Request, interrupt vector = " << HEXN(addr, 2));
				m_pendingInterruptRequests.push_back(addr);
				write8(_addr, _val & ~Hc);
			}
			break;
		case PeriphAddress::HdiTXH:	writeTX(WordFlags::H, _val);	break;
		case PeriphAddress::HdiTXM:	writeTX(WordFlags::M, _val);	break;
		case PeriphAddress::HdiTXL:	writeTX(WordFlags::L, _val);	break;
		}
	}

	void Hdi08::write16(PeriphAddress _addr, uint16_t _val)
	{
		PeripheralBase::write16(_addr, _val);
	}

	bool Hdi08::pollInterruptRequest(uint8_t& _addr)
	{
		if(m_pendingInterruptRequests.empty())
			return false;

		_addr = m_pendingInterruptRequests.front();

		m_pendingInterruptRequests.pop_front();

		const auto val = read8(PeriphAddress::HdiCVR);
		write8(PeriphAddress::HdiCVR, val & ~Hc);

		return true;
	}

	void Hdi08::writeRx(uint32_t _word)
	{
		m_rxData.push_back(_word);
	}

	void Hdi08::exec(uint32_t _deltaCycles)
	{
		PeripheralBase::exec(_deltaCycles);

		if(m_rxData.empty())
			return;

		const auto isr = read8(PeriphAddress::HdiISR);

		if(isr & Rxdf)
			return;

		write8(PeriphAddress::HdiISR, isr | Rxdf);
		m_readFlags = WordFlags::Mask;

		if(read8(PeriphAddress::HdiICR) & Hreq)
		{
			const auto ivr = read8(PeriphAddress::HdiIVR);
			assert(false);
		}
	}

	void Hdi08::writeTX(WordFlags _index, uint8_t _val)
	{
		m_txBytes[static_cast<uint32_t>(_index)] = _val;

		addIndex(m_writtenFlags, _index);

		if(m_writtenFlags != WordFlags::Mask)
			return;

		const uint32_t h = m_txBytes[0];
		const uint32_t m = m_txBytes[1];
		const uint32_t l = m_txBytes[2];

		const auto word = littleEndian() ? 
			                  l << 16 | m << 8 | h :
			                  h << 16 | m << 8 | l;

		m_txData.push_back(word);

		m_writtenFlags = WordFlags::None;
	}

	Hdi08::WordFlags Hdi08::makeMask(WordFlags _index)
	{
		return static_cast<WordFlags>(1 << static_cast<uint32_t>(_index));
	}

	void Hdi08::addIndex(WordFlags& _existing, WordFlags _indexToAdd)
	{
		_existing = static_cast<WordFlags>(static_cast<uint32_t>(_existing) | static_cast<uint32_t>(makeMask(_indexToAdd)));
	}

	void Hdi08::removeIndex(WordFlags& _existing, WordFlags _indexToRemove)
	{
		_existing = static_cast<WordFlags>(static_cast<uint32_t>(_existing) & ~static_cast<uint32_t>(makeMask(_indexToRemove)));
	}

	uint8_t Hdi08::littleEndian()
	{
		return read8(PeriphAddress::HdiICR) & static_cast<uint8_t>(Hlend);
	}

	uint8_t Hdi08::readRX(WordFlags _index)
	{
		if(m_rxData.empty())
		{
			LOG("Empty read of RX");
			return 0;
		}

		const auto word = m_rxData.front();

		std::array<uint8_t, 3> bytes{};

		const auto le = littleEndian();
		if(le)
		{
			bytes[0] = (word >> 16) & 0xff;
			bytes[1] = (word >> 8) & 0xff;
			bytes[2] = (word) & 0xff;
		}
		else
		{
			bytes[0] = (word) & 0xff;
			bytes[1] = (word >> 8) & 0xff;
			bytes[2] = (word >> 16) & 0xff;
		}

		removeIndex(m_readFlags, _index);

		if(m_readFlags == WordFlags::None)
		{
			write8(PeriphAddress::HdiISR, read8(PeriphAddress::HdiISR) & ~(Rxdf));
			m_rxData.pop_front();
		}

		return bytes[static_cast<uint32_t>(_index)];
	}
}
