#include "hdi08List.h"

namespace virusLib
{
	void Hdi08List::addHDI08(dsp56k::HDI08& _hdi08)
	{
		m_queues.emplace_back(_hdi08);
	}

	bool Hdi08List::rxEmpty() const
	{
		for (const auto& h : m_queues)
		{
			if(!h.rxEmpty())
				return false;
		}
		return true;
	}

	void Hdi08List::exec()
	{
		for (auto& h : m_queues)
			h.exec();
	}

	void Hdi08List::writeRX(const dsp56k::TWord* _buf, size_t _length)
	{
		for (auto& h : m_queues)
			h.writeRX(_buf, _length);
	}

	void Hdi08List::writeHostFlags(uint8_t _flag0, uint8_t _flag1)
	{
		for (auto& h : m_queues)
			h.writeHostFlags(_flag0, _flag1);
	}
}
