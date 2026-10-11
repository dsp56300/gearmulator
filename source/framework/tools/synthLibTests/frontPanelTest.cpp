#include "synthLibTests.h"

#include <iostream>

#include "synthLib/frontPanelSysex.h"

namespace
{
	using namespace synthLib;

	SysexBuffer frame(std::initializer_list<uint8_t> _payload, const fp::Type _type)
	{
		SysexBuffer s(std::begin(fp::g_header), std::end(fp::g_header));
		s.push_back(static_cast<uint8_t>(_type));
		s.insert(s.end(), _payload.begin(), _payload.end());
		s.push_back(0xf7);
		return s;
	}

	bool isSevenBitClean(const SysexBuffer& _s)
	{
		for(size_t i = 1; i + 1 < _s.size(); ++i)
		{
			if(_s[i] & 0x80)
				return false;
		}
		return true;
	}

	void testLcdText()
	{
		// 40 x 2 with every kind of cell: CGRAM characters 8-15, ASCII and >= 0x80
		std::vector<uint8_t> cells(80);
		for(size_t i = 0; i < cells.size(); ++i)
			cells[i] = static_cast<uint8_t>(i < 8 ? 8 + i : (i * 37) & 0xff);
		cells[79] = 0xff;

		std::vector<SMidiEvent> out;
		fp::sendLcdText(out, 40, 2, cells.data());
		TEST_ASSERT(out.size() == 1 && out[0].source == MidiEventSource::Internal);
		TEST_ASSERT(fp::isFrontPanel(out[0].sysex) && isSevenBitClean(out[0].sysex));

		FrontPanelTransport t;
		uint32_t cols = 0, rows = 0;
		std::vector<uint8_t> received;
		t.evLcdText.addListener([&](const uint32_t _c, const uint32_t _r, const std::vector<uint8_t>& _cells) { cols = _c; rows = _r; received = _cells; });
		TEST_ASSERT(t.receive(out[0].sysex));
		TEST_ASSERT(cols == 40 && rows == 2 && received == cells);

		// retained: a listener added later gets it at once
		bool late = false;
		t.evLcdText.addListener([&](uint32_t, uint32_t, const std::vector<uint8_t>& _cells) { late = _cells == cells; });
		TEST_ASSERT(late);

		// 20 x 2, the size of the microQ and JP-8000 displays, 40 cells are not a multiple of 7
		std::vector<uint8_t> small(40, 'A');
		out.clear();
		fp::sendLcdText(out, 20, 2, small.data());
		TEST_ASSERT(t.receive(out[0].sysex));
		TEST_ASSERT(cols == 20 && rows == 2 && received == small);
	}

	void testCgRam()
	{
		std::array<uint8_t, fp::g_cgRamSize> cg{};
		for(size_t i = 0; i < cg.size(); ++i)
			cg[i] = static_cast<uint8_t>(i & 0x1f);

		std::vector<SMidiEvent> out;
		fp::sendLcdCgRam(out, cg);
		TEST_ASSERT(isSevenBitClean(out[0].sysex));

		FrontPanelTransport t;
		std::array<uint8_t, fp::g_cgRamSize> received{};
		t.evLcdCgRam.addListener([&](const std::array<uint8_t, fp::g_cgRamSize>& _cg) { received = _cg; });
		TEST_ASSERT(t.receive(out[0].sysex));
		TEST_ASSERT(received == cg);
	}

	void testLeds()
	{
		FrontPanelTransport t;
		std::vector<bool> received;
		t.evLedMask.addListener([&](const std::vector<bool>& _leds) { received = _leds; });

		// microQ: 26 LEDs, Power is 25 = byte 3, bit 4
		std::vector<bool> mq(26, false);
		mq[25] = true;
		std::vector<SMidiEvent> out;
		fp::sendLedMask(out, mq);
		const auto& s = out[0].sysex;
		TEST_ASSERT(s[fp::g_typeIndex + 1] == 26);
		TEST_ASSERT(s[fp::g_typeIndex + 2 + 3] == (1 << 4));
		TEST_ASSERT(t.receive(s) && received == mq);

		// JP-8000: 66 LEDs, the last one is byte 9, bit 2
		std::vector<bool> je(66, false);
		je[0] = je[65] = true;
		out.clear();
		fp::sendLedMask(out, je);
		TEST_ASSERT(out[0].sysex[fp::g_typeIndex + 2 + 9] == (1 << 2));
		TEST_ASSERT(t.receive(out[0].sysex) && received == je);
	}

	void testInput()
	{
		FrontPanelTransport t;
		uint32_t button = 0; bool pressed = false;
		uint32_t encoder = 0; int32_t delta = 0;
		uint8_t page = 0, index = 0; int32_t value = 0;
		int requests = 0;
		t.evButton.addListener([&](const uint32_t _i, const bool _p) { button = _i; pressed = _p; });
		t.evEncoder.addListener([&](const uint32_t _i, const int32_t _d) { encoder = _i; delta = _d; });
		t.evParam.addListener([&](const uint8_t _p, const uint8_t _i, const int32_t _v) { page = _p; index = _i; value = _v; });
		t.evRequestState.addListener([&](uint8_t) { ++requests; });

		std::vector<SMidiEvent> out;
		fp::sendButton(out, 0x1234, true);
		fp::sendEncoder(out, 6, -3);
		fp::sendParam(out, 6, 0, -100);
		fp::sendRequestState(out);
		for(const auto& e : out)
			TEST_ASSERT(t.receive(e.sysex) && isSevenBitClean(e.sysex));

		TEST_ASSERT(button == 0x1234 && pressed);
		TEST_ASSERT(encoder == 6 && delta == -3);
		TEST_ASSERT(page == 6 && index == 0 && value == -100);
		TEST_ASSERT(requests == 1);

		// JE8086 persisted its master volume as F0 7D 05 <page:2> <index:2> <value:8> F7 and pressed buttons with
		// F0 7D 02 <index:4> <pressed> F7: the payloads are the same, the old frames only need the new header
		out.clear();
		fp::sendParam(out, 6, 0, 100);
		const SysexBuffer legacyParam = {0xf0, 0x7d, 0x05, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 6, 4, 0xf7};
		TEST_ASSERT(std::equal(out[0].sysex.begin() + fp::g_typeIndex + 1, out[0].sysex.end(), legacyParam.begin() + 3));

		out.clear();
		fp::sendButton(out, 0x2a, false);
		const SysexBuffer legacyButton = {0xf0, 0x7d, 0x02, 0, 0, 2, 0xa, 0, 0xf7};
		TEST_ASSERT(std::equal(out[0].sysex.begin() + fp::g_typeIndex + 1, out[0].sysex.end(), legacyButton.begin() + 3));
	}

	void testRejects()
	{
		FrontPanelTransport t;
		int events = 0;
		t.evLcdText.addListener([&](uint32_t, uint32_t, const std::vector<uint8_t>&) { ++events; });
		t.evButton.addListener([&](uint32_t, bool) { ++events; });
		t.evLedMask.addListener([&](const std::vector<bool>&) { ++events; });
		uint8_t productType = 0;
		t.evProduct.addListener([&](const uint8_t _type, const SysexBuffer&) { productType = _type; });

		// not ours: a microQ panel frame whose command byte equals the tag's 0x53, a JE8086 frame, a short tag
		TEST_ASSERT(!t.receive({0xf0, 0x3e, 0x10, 0x00, 0x53, 0xf7}));
		TEST_ASSERT(!t.receive({0xf0, 0x7d, 0x05, 0x00, 0xf7}));
		TEST_ASSERT(!t.receive({0xf0, 0x7d, 0x54, 0x75, 0x53, 0xf7}));
		TEST_ASSERT(!t.receive({0xf0, 0x7d, 0x54, 0x75, 0x53, 0x04, 0x00}));	// no F7

		// ours, consumed but not understood: unknown and reserved types, truncated and oversized payloads
		TEST_ASSERT(t.receive(frame({}, static_cast<fp::Type>(0x5f))));
		TEST_ASSERT(t.receive(frame({1}, fp::Type::Pot)));
		TEST_ASSERT(t.receive(frame({}, fp::Type::LcdText)));
		TEST_ASSERT(t.receive(frame({20, 2, 0, 0}, fp::Type::LcdText)));
		TEST_ASSERT(t.receive(frame({0, 0, 0, 1}, fp::Type::Button)));
		TEST_ASSERT(t.receive(frame({0, 0, 0, 1, 1, 1}, fp::Type::Button)));
		TEST_ASSERT(t.receive(frame({8}, fp::Type::LedMask)));
		TEST_ASSERT(t.receive(frame({8, 1, 1, 1}, fp::Type::LedMask)));
		TEST_ASSERT(events == 0);

		// product range
		TEST_ASSERT(t.receive(frame({1, 2, 3}, static_cast<fp::Type>(0x61))));
		TEST_ASSERT(productType == 0x61);
	}
}

void testFrontPanel()
{
	std::cout << "Testing front panel sysex..." << std::endl;

	testLcdText();
	testCgRam();
	testLeds();
	testInput();
	testRejects();

	std::cout << "  front panel sysex tests passed" << std::endl;
}
