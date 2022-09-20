#include "hdi08.h"

#include <cassert>

#include "logging.h"

namespace mc68k
{
	constexpr uint32_t g_readTimeoutCycles = 50;

	Hdi08::Hdi08()
	{
		setWriteTxCallback(nullptr);
		setWriteIrqCallback(nullptr);
		setReadIsrCallback(nullptr);

		write8(PeriphAddress::HdiIVR, 0xf);
	}

	uint8_t Hdi08::read8(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::HdiICR:
			return icr();
		case PeriphAddress::HdiISR:
			return isr();
		case PeriphAddress::HdiTXH:	return readRX(WordFlags::H);
		case PeriphAddress::HdiTXM:	return readRX(WordFlags::M);
		case PeriphAddress::HdiTXL:	return readRX(WordFlags::L);
		case PeriphAddress::HdiCVR:
			return PeripheralBase::read8(_addr);
		}
		const auto r = PeripheralBase::read8(_addr);
//		LOG("read8 addr=" << HEXN(_addr, 8));
		return r;
	}

	uint16_t Hdi08::read16(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::HdiUnused4:
			return read8(PeriphAddress::HdiTXH);
		case PeriphAddress::HdiTXM:
			{
				uint16_t r = (static_cast<uint16_t>(read8(PeriphAddress::HdiTXM)) << static_cast<uint16_t>(8));
				r |= static_cast<uint16_t>(read8(PeriphAddress::HdiTXL));
				return r;
			}
		}
		LOG("read16 addr=" << HEXN(_addr, 8));
		return PeripheralBase::read16(_addr);
	}

	void Hdi08::write8(PeriphAddress _addr, uint8_t _val)
	{
		PeripheralBase::write8(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::HdiISR:
//			LOG("HDI08 ISR set to " << HEXN(_val,2));
			return;
		case PeriphAddress::HdiICR:
//			LOG("HDI08 ICR set to " << HEXN(_val,2));
			if(_val & Init)
			{
				LOG("HDI08 Initialization, HREQ=" << (_val & Rreq) << ", TREQ=" << (_val & Treq));
			}
			return;
		case PeriphAddress::HdiCVR:
			if(_val & Hc)
			{
				const auto addr = static_cast<uint8_t>((_val & Hv) << 1);
//				LOG("HDI08 Host Vector Interrupt Request, interrupt vector = " << HEXN(addr, 2));
				m_writeIrqCallback(addr);

				const auto val = read8(PeriphAddress::HdiCVR);
				PeripheralBase::write8(PeriphAddress::HdiCVR, val & ~Hc);

//				write8(_addr, _val & ~Hc);
			}
			return;
		case PeriphAddress::HdiTXH:	writeTX(WordFlags::H, _val);	return;
		case PeriphAddress::HdiTXM:	writeTX(WordFlags::M, _val);	return;
		case PeriphAddress::HdiTXL:	writeTX(WordFlags::L, _val);	return;
		}
		LOG("write8 addr=" << HEXN(_addr, 8) << ", val=" << HEXN(static_cast<int>(_val),2));
	}

	void Hdi08::write16(PeriphAddress _addr, uint16_t _val)
	{
		PeripheralBase::write16(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::HdiUnused4:
			write8(PeriphAddress::HdiTXH, _val & 0xff);
			return;
		case PeriphAddress::HdiTXM:
			write8(PeriphAddress::HdiTXM, _val >> 8);
			write8(PeriphAddress::HdiTXL, _val & 0xff);
			break;
		default:
			LOG("write16 addr=" << HEXN(_addr, 8) << ", val=" << HEXN(_val,4));
			break;
		}
	}

	bool Hdi08::pollInterruptRequest(uint8_t& _addr)
	{
		if(m_pendingInterruptRequests.empty())
			return false;

		_addr = m_pendingInterruptRequests.front();

		m_pendingInterruptRequests.pop_front();

		return true;
	}

	void Hdi08::writeRx(uint32_t _word)
	{
//		LOG("HDI writeRX=" << HEX(_word));

		m_rxData.push_back(_word);

		const auto s = isr();

		if(!(s & Rxdf))
			pollRx();
	}

	void Hdi08::exec(uint32_t _deltaCycles)
	{
		PeripheralBase::exec(_deltaCycles);

		auto isr = PeripheralBase::read8(PeriphAddress::HdiISR);//Hdi08::isr();

		if(!(isr & Rxdf))
			return;

		if(m_rxData.empty())
			return;

		m_readTimeoutCycles += _deltaCycles;

		if(m_readTimeoutCycles >= g_readTimeoutCycles)
		{
			LOG("HDI RX read timeout on byte " << HEXN(m_rxd, 2));
			isr &= ~Rxdf;
			write8(PeriphAddress::HdiISR, isr);
			pollRx();
		}
	}

	bool Hdi08::canReceiveData()
	{
		return (isr() & Rxdf) == 0;
	}

	void Hdi08::setWriteTxCallback(const CallbackWriteTx& _writeTxCallback)
	{
		if(_writeTxCallback)
		{
			m_writeTxCallback = _writeTxCallback;
		}
		else
		{
			m_writeTxCallback = [this] (const uint32_t _word)
			{
				m_txData.push_back(_word);
			};
		}
	}

	void Hdi08::setWriteIrqCallback(const CallbackWriteIrq& _writeIrqCallback)
	{
		if(_writeIrqCallback)
		{
			m_writeIrqCallback = _writeIrqCallback;
		}
		else
		{
			m_writeIrqCallback = [this](uint8_t _irq)
			{
				m_pendingInterruptRequests.push_back(_irq);
			};
		}
	}

	void Hdi08::setReadIsrCallback(const CallbackReadIsr& _readIsrCallback)
	{
		if(_readIsrCallback)
		{
			m_readIsrCallback = _readIsrCallback;
		}
		else
		{
			m_readIsrCallback = [](const uint8_t _isr) { return _isr; };
		}
	}

	void Hdi08::writeTX(WordFlags _index, uint8_t _val)
	{
		m_txBytes[static_cast<uint32_t>(_index)] = _val;

		const auto lastWritten = m_writtenFlags;
		addIndex(m_writtenFlags, _index);
		assert(lastWritten != m_writtenFlags && "byte written twice!");

#if 1
		if(m_writtenFlags != WordFlags::Mask)
			return;
#else
		const auto le = littleEndian();
		if((le && _index != WordFlags::L) || (!le && _index != WordFlags::H))
			return;
#endif

		const uint32_t h = m_txBytes[0];
		const uint32_t m = m_txBytes[1];
		const uint32_t l = m_txBytes[2];

		const auto word = littleEndian() ? 
			                  l << 16 | m << 8 | h :
			                  h << 16 | m << 8 | l;

		m_writeTxCallback(word);

//		LOG("HDI TX: " << HEXN(word, 6));

		m_txBytes.fill(0);

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
		const auto hasRX = isr() & Rxdf;

		if(!hasRX)
		{
			m_rxEmptyCallback(true);

			const auto s = isr();

			if(!(s & Rxdf))
			{
//				LOG("Empty read of RX");
				return 0;
			}
		}

		const auto word = m_rxd;

		std::array<uint8_t, 3> bytes{};

		const auto le = littleEndian();

		auto pop = [&]()
		{
			write8(PeriphAddress::HdiISR, isr() & ~(Rxdf));
			m_rxEmptyCallback(false);
		};

		if(le)
		{
			bytes[0] = (word) & 0xff;
			bytes[1] = (word >> 8) & 0xff;
			bytes[2] = (word >> 16) & 0xff;

			if(_index == WordFlags::H)
				pop();
		}
		else
		{
			bytes[0] = (word >> 16) & 0xff;
			bytes[1] = (word >> 8) & 0xff;
			bytes[2] = (word) & 0xff;

			if(_index == WordFlags::L)
				pop();
		}

		removeIndex(m_readFlags, _index);

		return bytes[static_cast<uint32_t>(_index)];
	}

	bool Hdi08::pollRx()
	{
		if(m_rxData.empty())
			return false;

		m_readTimeoutCycles = 0;
		m_rxd = m_rxData.front();
		m_rxData.pop_front();

		auto isr = Hdi08::isr();

		write8(PeriphAddress::HdiISR, isr | Rxdf);
		m_readFlags = WordFlags::Mask;

		if(isr & Hreq)
		{
			const auto ivr = read8(PeriphAddress::HdiIVR);
			assert(false);
		}

		return true;
	}
}
