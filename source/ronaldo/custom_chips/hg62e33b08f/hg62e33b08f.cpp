#include "hg62e33b08f.h"

namespace hg62e33b08fLib
{
	namespace
	{
		constexpr uint8_t g_regSerialStatus = 0x14;
		constexpr uint8_t g_regIrqMask = 0x15;
		constexpr uint8_t g_regDmaCount = 0x18;
		constexpr uint8_t g_regDmaAddressLow = 0x1a;
		constexpr uint8_t g_regDmaAddressHigh = 0x1c;
		constexpr uint8_t g_regDmaControl = 0x1d;
		constexpr uint8_t g_serialTxReady = 0x02;
	}

	HG62E33B08F::HG62E33B08F()
	{
		reset();
	}

	void HG62E33B08F::reset()
	{
		m_regs.fill(0);
		for (uint8_t window = 0; window < 8; ++window)
			setWord(static_cast<uint8_t>(window * 2), BootBank);
		m_sources = m_acknowledged = 0;
		m_dmaActive = false;
		m_dmaAddress = 0;
		m_dmaCount = 0;
		m_dmaControl = 0;
		setLine(false);
		if (m_bankCallback)
			for (unsigned window = 0; window < 4; ++window)
				m_bankCallback(window, bank(window));
	}

	void HG62E33B08F::setWord(const uint8_t _reg, const uint16_t _value)
	{
		m_regs[_reg & 0x1e] = static_cast<uint8_t>(_value);
		m_regs[(_reg & 0x1e) + 1] = static_cast<uint8_t>(_value >> 8);
	}

	uint8_t HG62E33B08F::peek(const uint8_t _reg) const
	{
		const uint8_t reg = _reg & (RegisterCount - 1);
		return reg == g_regSerialStatus ? static_cast<uint8_t>(m_regs[reg] | g_serialTxReady) : m_regs[reg];
	}

	uint8_t HG62E33B08F::read(const uint8_t _reg)
	{
		const uint8_t reg = _reg & (RegisterCount - 1);
		const uint8_t value = peek(reg);
		if (reg == g_regDmaControl)
			setSource(DmaSource, false);
		return value;
	}

	void HG62E33B08F::write(const uint8_t _reg, const uint8_t _value)
	{
		const uint8_t reg = _reg & (RegisterCount - 1);
		m_regs[reg] = _value;

		if (reg < 0x10)
		{
			// The bank registers are hit a byte at a time as well as by word,
			// so each byte re-derives its window.
			const unsigned window = reg >> 1;
			if (window < 4 && m_bankCallback)
				m_bankCallback(window, bank(window));
			return;
		}

		switch (reg)
		{
		case g_regIrqMask:
			updateIrq();
			break;
		case g_regDmaControl:
			// The firmware programs count and address first; this byte arms.
			m_dmaCount = word(g_regDmaCount);
			m_dmaAddress = word(g_regDmaAddressLow) | (uint32_t(m_regs[g_regDmaAddressHigh]) << 16);
			m_dmaControl = _value;
			m_dmaActive = m_dmaCount != 0;
			setSource(DmaSource, false);
			break;
		default:
			break;
		}
	}

	bool HG62E33B08F::dmaStep()
	{
		if (!m_dmaActive)
			return false;
		++m_dmaAddress;
		--m_dmaCount;
		setWord(g_regDmaCount, m_dmaCount);
		setWord(g_regDmaAddressLow, static_cast<uint16_t>(m_dmaAddress));
		m_regs[g_regDmaAddressHigh] = static_cast<uint8_t>(m_dmaAddress >> 16);
		if (m_dmaCount)
			return false;
		m_dmaActive = false;
		setSource(DmaSource, true);
		return true;
	}

	void HG62E33B08F::setSource(const unsigned _source, const bool _level)
	{
		if (_source >= SourceCount)
			return;
		const uint8_t bit = static_cast<uint8_t>(1u << _source);
		const uint8_t sources = _level ? (m_sources | bit) : (m_sources & ~bit);
		if (sources == m_sources)
			return;
		m_sources = sources;
		updateIrq();
	}

	uint8_t HG62E33B08F::pending() const
	{
		return static_cast<uint8_t>(m_sources & m_regs[g_regIrqMask] & ~m_acknowledged);
	}

	void HG62E33B08F::updateIrq()
	{
		// An acknowledged source stays quiet until its input drops.
		m_acknowledged &= m_sources & m_regs[g_regIrqMask];
		setLine(pending() != 0);
	}

	void HG62E33B08F::setLine(const bool _level)
	{
		if (m_irq == _level)
			return;
		m_irq = _level;
		if (m_irqCallback)
			m_irqCallback(_level);
	}

	uint16_t HG62E33B08F::vector(const uint16_t _tableWord)
	{
		const uint8_t requests = pending();
		if (!m_irq || !requests)
			return _tableWord;
		unsigned source = 0;
		while (!(requests & (1u << source)))
			++source;
		m_acknowledged |= static_cast<uint8_t>(1u << source);
		// EXTINT1 takes edges: drop the line, then raise it again for any
		// other source still waiting.
		setLine(false);
		updateIrq();
		return static_cast<uint16_t>((_tableWord & ~0x1cu) | (source << 2));
	}
}
