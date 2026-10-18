#include "hg62e33b08f.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
	using hg62e33b08fLib::HG62E33B08F;

	void require(const bool _condition, const char* _message)
	{
		if (_condition) return;
		std::fprintf(stderr, "hg62e33b08f test failed: %s\n", _message);
		std::exit(1);
	}

	void writeWord(HG62E33B08F& _gate, const uint8_t _reg, const uint16_t _value)
	{
		_gate.write(_reg, static_cast<uint8_t>(_value));
		_gate.write(static_cast<uint8_t>(_reg + 1), static_cast<uint8_t>(_value >> 8));
	}

	void testBanks()
	{
		HG62E33B08F gate;
		std::vector<std::pair<unsigned, uint16_t>> switched;
		gate.setBankCallback([&](const unsigned _window, const uint16_t _bank) { switched.emplace_back(_window, _bank); });

		for (unsigned window = 0; window < 8; ++window)
			require(gate.bank(window) == HG62E33B08F::BootBank, "power-on maps the boot ROM everywhere");
		require(gate.physical(0x2080, true) == 0x780 * 0x400 + 0x2080, "reset vector through instruction window 0");

		// Code at RAM bank 0x2e in window 2, data at bank 0xa1 there.
		writeWord(gate, 0x04, 0x002e);
		writeWord(gate, 0x0c, 0x00a1);
		require(gate.physical(0x8081, true) == 0x2e * 0x400 + 0x0081, "instruction window 2");
		require(gate.physical(0x8081, false) == 0xa1 * 0x400 + 0x0081, "data window 2");
		require(!switched.empty() && switched.back() == std::make_pair(2u, uint16_t(0x002e)), "instruction bank change reported");
		const auto reported = switched.size();
		writeWord(gate, 0x0e, 0x0474);
		require(switched.size() == reported, "data banks are not reported");
	}

	void testInterrupts()
	{
		HG62E33B08F gate;
		std::vector<bool> edges;
		gate.setIrqCallback([&](const bool _level) { edges.push_back(_level); });

		gate.setSource(2, true);
		require(!gate.irq(), "masked source stays quiet");
		gate.write(0x15, 0x16);	// panel, FDC, DMA
		require(gate.irq(), "unmasked source raises INT");

		gate.setSource(1, true);
		require(gate.vector(0x0120) == (0x0120 | (1 << 2)), "lowest source first");
		require(gate.irq(), "the other source raises INT again");
		require(edges == std::vector<bool>({true, false, true}), "a fresh edge for the waiting source");
		require(gate.vector(0x0120) == (0x0120 | (2 << 2)), "then the next one");
		require(!gate.irq(), "both acknowledged");
		require(gate.vector(0x0120) == 0x0120, "the table word passes untouched while INT is low");

		// An acknowledged source stays quiet until it drops and rises again.
		gate.setSource(1, false);
		gate.setSource(1, true);
		require(gate.irq(), "a new request after the input dropped");
	}

	void testDma()
	{
		HG62E33B08F gate;
		gate.write(0x15, 1u << HG62E33B08F::DmaSource);
		writeWord(gate, 0x18, 3);
		writeWord(gate, 0x1a, 0xfffe);
		gate.write(0x1c, 0x01);
		require(!gate.dmaActive(), "not armed before the control byte");
		gate.write(0x1d, 0x87);
		require(gate.dmaActive() && gate.dmaToMemory(), "armed, device to memory");
		require(gate.dmaAddress() == 0x01fffe, "24-bit address");
		require(!gate.dmaStep() && !gate.dmaStep(), "two bytes in");
		require(gate.dmaAddress() == 0x020000, "the address carries into the high byte");
		require(gate.peek(0x1c) == 0x02 && gate.peek(0x18) == 1, "registers follow the channel");
		require(gate.dmaStep() && !gate.dmaActive(), "the last byte ends the transfer");
		require(gate.irq(), "completion interrupts");
		require(gate.read(0x1d) == 0x87 && !gate.irq(), "reading the control byte acknowledges");
	}
}

int main()
{
	testBanks();
	testInterrupts();
	testDma();
	std::printf("hg62e33b08f test passed\n");
	return 0;
}
