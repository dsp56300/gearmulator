#include "am29f.h"
#include "baseLib/os.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

// hwLib::Am29f against the AMD datasheets (Am29F010B, Am29F200B, Am29F400B, Am29LV800B). Every expectation is the
// chip's behaviour, not the implementation's: a failing check names a difference from the real part.

namespace
{
	using namespace hwLib;

	int g_checks = 0;
	int g_failures = 0;

	void check(const bool _condition, const char* _test, const char* _what)
	{
		++g_checks;
		if(_condition)
			return;
		++g_failures;
		printf("FAILED %s: %s\n", _test, _what);
	}

	#define CHECK(_test, _condition) check((_condition), _test, #_condition)

	// A chip over its own memory, with the command sequences of the datasheets. Word mode chips on a 16 bit bus see
	// the command cycles at byte offsets $AAA/$554, which is what _bitreversedCmdAddr gives
	struct Chip
	{
		static constexpr size_t GuardSize = 4;	// behind the chip's memory, nothing may write there

		std::vector<uint8_t> buffer;
		size_t size;
		Am29f am29f;
		uint16_t cmd555;
		uint16_t cmd2aa;

		explicit Chip(const size_t _size, const bool _wordOnWordBus = false, const bool _byteWide = false, const bool _useWriteEnable = false)
			: buffer(_size + GuardSize, 0xff)
			, size(_size)
			, am29f(buffer.data(), _size, _useWriteEnable, _wordOnWordBus, _byteWide)
			, cmd555(_wordOnWordBus ? 0xaaa : 0x555)
			, cmd2aa(_wordOnWordBus ? 0x554 : 0x2aa)
		{
			std::fill(buffer.begin() + static_cast<ptrdiff_t>(_size), buffer.end(), 0xee);
		}

		uint8_t at(const size_t _addr) const { return buffer[_addr]; }
		void fill(const uint8_t _value) { std::fill_n(buffer.begin(), size, _value); }
		bool guardIntact() const { return std::all_of(buffer.begin() + static_cast<ptrdiff_t>(size), buffer.end(), [](const uint8_t _b) { return _b == 0xee; }); }

		void unlock()
		{
			am29f.write(cmd555, 0xaa);
			am29f.write(cmd2aa, 0x55);
		}

		void program(const uint32_t _addr, const uint16_t _data)
		{
			unlock();
			am29f.write(cmd555, 0xa0);
			am29f.write(_addr, _data);
		}

		void eraseSector(const uint32_t _addr)
		{
			unlock();
			am29f.write(cmd555, 0x80);
			unlock();
			am29f.write(_addr, 0x30);
		}

		void eraseChip()
		{
			unlock();
			am29f.write(cmd555, 0x80);
			unlock();
			am29f.write(cmd555, 0x10);
		}

		uint16_t word(const uint32_t _addr) const
		{
			return static_cast<uint16_t>(buffer[_addr] << 8 | buffer[_addr + 1]);
		}

		bool allEqual(const size_t _begin, const size_t _end, const uint8_t _value) const
		{
			for(size_t i = _begin; i < _end; ++i)
			{
				if(buffer[i] != _value)
					return false;
			}
			return true;
		}
	};

	// ---- Program ----

	void testProgramWord()
	{
		// the host sees a word as big endian bytes, the order of a 68k bus
		for(const bool bitreversed : {false, true})
		{
			Chip c(0x10000, bitreversed);
			c.program(0x100, 0x1234);
			CHECK("programWord", c.at(0x100) == 0x12 && c.at(0x101) == 0x34);
			CHECK("programWord", c.allEqual(0, 0x100, 0xff) && c.allEqual(0x102, c.size, 0xff));
		}
	}

	void testProgramByte()
	{
		Chip c(0x10000, false, true);
		c.program(0x101, 0x5a);
		CHECK("programByte", c.at(0x101) == 0x5a);
		CHECK("programByte", c.at(0x100) == 0xff && c.at(0x102) == 0xff);
	}

	void testProgramOnlyClearsBits()
	{
		// "a bit cannot be programmed from a 0 back to a 1", only an erase sets it again
		Chip c(0x10000);
		c.program(0x200, 0x0ff0);
		c.program(0x200, 0xf00f);
		CHECK("programOnlyClearsBits", c.word(0x200) == 0x0000);
		c.program(0x202, 0x5555);
		c.program(0x202, 0xffff);
		CHECK("programOnlyClearsBits", c.word(0x202) == 0x5555);
	}

	void testProgramDataThatLooksLikeACommand()
	{
		// The cycle after a complete program command is its data, whatever the data and the address are. Data AA at
		// the $555 command address looks like cycle 4 of the erase sequences, data F0 like a reset
		for(const bool bitreversed : {false, true})
		{
			for(const uint16_t data : {0x00aa, 0x55aa, 0x0055, 0x0080, 0x0090, 0x00a0, 0x0020, 0x0010, 0x0030, 0x00f0})
			{
				Chip c(0x10000, bitreversed);
				for(const uint32_t addr : {static_cast<uint32_t>(c.cmd555), static_cast<uint32_t>(c.cmd2aa), 0x1000u + c.cmd555})
				{
					c.program(addr, data);
					c.program(addr + 0x2000, 0x1234);	// and the next program still works
					char what[128];
					snprintf(what, sizeof(what), "data %04x at %04x (bitreversed %d)", data, addr, bitreversed ? 1 : 0);
					CHECK("programDataThatLooksLikeACommand", c.word(addr) == data && c.word(addr + 0x2000) == 0x1234);
					if(c.word(addr) != data || c.word(addr + 0x2000) != 0x1234)
						printf("    %s\n", what);
				}
			}
		}
	}

	void testPlainWritesDoNotChangeMemory()
	{
		Chip c(0x10000);
		c.am29f.write(0x300, 0x0000);
		c.am29f.write(0x302, 0x1234);
		CHECK("plainWrites", c.allEqual(0, c.size, 0xff));
	}

	void testProgramOutOfRangeIsIgnored()
	{
		Chip c(0x10000);
		c.program(0x10000, 0x0000);
		c.program(0x20000 + 0x100, 0x0000);
		CHECK("programOutOfRange", c.allEqual(0, c.size, 0xff) && c.guardIntact());

		// a word at the last byte would reach one byte past the memory
		c.program(0xffff, 0x0000);
		CHECK("programOutOfRange", c.guardIntact());

		c.program(0x100, 0x1234);
		CHECK("programOutOfRange", c.word(0x100) == 0x1234);
	}

	// ---- Command sequences ----

	void testResetAbortsASequence()
	{
		Chip c(0x10000);
		c.unlock();
		c.am29f.write(0, 0xf0);
		c.am29f.write(c.cmd555, 0xa0);	// not a command without the unlock cycles
		c.am29f.write(0x400, 0x0000);
		CHECK("resetAbortsASequence", c.allEqual(0, c.size, 0xff));
		c.program(0x400, 0x1234);
		CHECK("resetAbortsASequence", c.word(0x400) == 0x1234);
	}

	void testWrongCycleAbortsASequence()
	{
		Chip c(0x10000);
		c.am29f.write(c.cmd555, 0xaa);
		c.am29f.write(c.cmd2aa, 0x12);	// not the second unlock cycle
		c.am29f.write(c.cmd555, 0xa0);
		c.am29f.write(0x500, 0x0000);
		CHECK("wrongCycleAbortsASequence", c.allEqual(0, c.size, 0xff));
		c.program(0x500, 0x4321);
		CHECK("wrongCycleAbortsASequence", c.word(0x500) == 0x4321);
	}

	void testJedecCommandAddresses()
	{
		// The Am29F040, Am29F400B and Am29LV800B decode A10-A0 in command cycles: $555/$2AA and the JEDEC $5555/$2AAA
		// both reach them. The Am29F010 decodes A14-A0 and needs $5555/$2AAA, which the A10-A0 chips accept as well
		Chip c(0x10000);
		c.am29f.write(0x5555, 0xaa);
		c.am29f.write(0x2aaa, 0x55);
		c.am29f.write(0x5555, 0xa0);
		c.am29f.write(0x600, 0x1234);
		CHECK("jedecCommandAddresses", c.word(0x600) == 0x1234);
	}

	// ---- Erase ----

	void testSectorErase()
	{
		// the default sector map is the Am29F400BT one: 64 KB at $10000
		Chip c(0x80000);
		c.fill(0);
		c.eraseSector(0x10000);
		CHECK("sectorErase", c.allEqual(0x10000, 0x20000, 0xff));
		CHECK("sectorErase", c.allEqual(0, 0x10000, 0) && c.allEqual(0x20000, c.size, 0));
	}

	void testChipErase()
	{
		Chip c(0x10000);
		c.fill(0);
		c.eraseChip();
		CHECK("chipErase", c.allEqual(0, c.size, 0xff));
	}

	using SectorMap = std::vector<std::pair<uint32_t, uint32_t>>;	// start, size in KB

	template<typename TErase>
	void checkSectorMap(const char* _name, const size_t _chipSize, const SectorMap& _map, TErase&& _erase)
	{
		// every sector erases exactly its own range, selected by its start and by any other address in it
		for(const bool anyAddress : {false, true})
		{
			int failed = 0;
			for(const auto& [start, kb] : _map)
			{
				Chip c(_chipSize);
				c.fill(0);
				const auto end = start + kb * 1024;
				const auto addr = anyAddress ? start + kb * 1024 / 2 + 0x123 : start;
				const bool res = _erase(c.am29f, addr);

				if(res && c.allEqual(start, end, 0xff) && c.allEqual(0, start, 0) && c.allEqual(end, _chipSize, 0))
					continue;
				if(!failed)
					printf("    %s: sector %05x, %u KB, erased at %05x: %s\n", _name, start, kb, addr, res ? "wrong range" : "refused");
				++failed;
			}
			CHECK(anyAddress ? "sectorMapAnyAddress" : "sectorMap", failed == 0);
			if(failed > 1)
				printf("    %s: %d sectors like this\n", _name, failed);
		}

		// and the map covers the chip without gaps or overlaps
		uint32_t next = 0;
		for(const auto& [start, kb] : _map)
		{
			CHECK("sectorMapCoversChip", start == next);
			next = start + kb * 1024;
		}
		CHECK("sectorMapCoversChip", next == _chipSize);
	}

	void testSectorMaps()
	{
		// Am29F010B: eight uniform 16 KB sectors
		checkSectorMap("1 Mbit", 0x20000, {{0x00000,16}, {0x04000,16}, {0x08000,16}, {0x0c000,16}, {0x10000,16}, {0x14000,16}, {0x18000,16}, {0x1c000,16}},
			[](const Am29f& _a, const uint32_t _addr) { return _a.eraseSector1Mbit(_addr); });

		// Am29F200BT: 64, 64, 64, 32, 8, 8, 16 KB
		checkSectorMap("2 Mbit top boot", 0x40000, {{0x00000,64}, {0x10000,64}, {0x20000,64}, {0x30000,32}, {0x38000,8}, {0x3a000,8}, {0x3c000,16}},
			[](const Am29f& _a, const uint32_t _addr) { return _a.eraseSector2MbitTopBoot(_addr); });

		// Am29F400BT: seven 64 KB sectors, then 32, 8, 8, 16 KB
		checkSectorMap("4 Mbit top boot", 0x80000, {{0x00000,64}, {0x10000,64}, {0x20000,64}, {0x30000,64}, {0x40000,64}, {0x50000,64}, {0x60000,64},
			{0x70000,32}, {0x78000,8}, {0x7a000,8}, {0x7c000,16}},
			[](const Am29f& _a, const uint32_t _addr) { return _a.eraseSector4MbitTopBoot(_addr); });

		// Am29LV800BB and MBM29LV800BA: 16, 8, 8, 32 KB, then fifteen 64 KB sectors
		SectorMap bottomBoot8Mbit = {{0x00000,16}, {0x04000,8}, {0x06000,8}, {0x08000,32}};
		for(uint32_t s = 0x10000; s < 0x100000; s += 0x10000)
			bottomBoot8Mbit.emplace_back(s, 64);
		checkSectorMap("8 Mbit bottom boot", 0x100000, bottomBoot8Mbit,
			[](const Am29f& _a, const uint32_t _addr) { return _a.eraseSector8MbitBottomBoot(_addr); });
	}

	void testEraseBeyondTheChipIsRefused()
	{
		// a map for a bigger chip than the memory must not erase past it
		Chip c(0x40000);
		c.fill(0);
		CHECK("eraseBeyondTheChip", !c.am29f.eraseSector4MbitTopBoot(0x70000));
		CHECK("eraseBeyondTheChip", c.allEqual(0, c.size, 0) && c.guardIntact());
	}

	// ---- Autoselect ----

	void testAutoselectByteMode()
	{
		Chip c(0x10000, false, true);
		c.am29f.setIds(0x01, 0xd5);
		uint8_t id = 0;
		CHECK("autoselectByteMode", !c.am29f.readAutoselect(0, id));

		c.unlock();
		c.am29f.write(c.cmd555, 0x90);
		CHECK("autoselectByteMode", c.am29f.readAutoselect(0, id) && id == 0x01);
		CHECK("autoselectByteMode", c.am29f.readAutoselect(1, id) && id == 0xd5);
		CHECK("autoselectByteMode", c.am29f.readAutoselect(2, id) && id == 0x00);		// sector not protected
		CHECK("autoselectByteMode", c.allEqual(0, c.size, 0xff));

		c.am29f.write(0, 0xf0);
		CHECK("autoselectByteMode", !c.am29f.readAutoselect(0, id));
	}

	void testAutoselectWordMode()
	{
		// a word mode chip on a 16 bit bus: the manufacturer id is word 0 (byte offsets 0 and 1), the device id
		// word 1 (byte offsets 2 and 3), the ids in the low bytes, which are the odd offsets on a big endian bus
		Chip c(0x10000, true);
		c.am29f.setIds(0x01, 0x5b);
		c.unlock();
		c.am29f.write(c.cmd555, 0x90);
		uint8_t id = 0;
		CHECK("autoselectWordMode", c.am29f.readAutoselect(1, id) && id == 0x01);
		CHECK("autoselectWordMode", c.am29f.readAutoselect(3, id) && id == 0x5b);
	}

	// ---- Unlock bypass ----

	void testUnlockBypass()
	{
		for(const bool bitreversed : {false, true})
		{
			Chip c(0x10000, bitreversed);
			c.unlock();
			c.am29f.write(c.cmd555, 0x20);

			// two cycles per program, at any address
			c.am29f.write(0x1234, 0xa0);
			c.am29f.write(0x700, 0x1111);
			c.am29f.write(0, 0xa0);
			c.am29f.write(0x702, 0x2222);
			CHECK("unlockBypass", c.word(0x700) == 0x1111 && c.word(0x702) == 0x2222);

			// data without A0 does not program
			c.am29f.write(0x704, 0x0000);
			CHECK("unlockBypass", c.word(0x704) == 0xffff);

			// 90, 00 returns to read mode, a program needs the unlock cycles again
			c.am29f.write(0, 0x90);
			c.am29f.write(0, 0x00);
			c.am29f.write(0, 0xa0);
			c.am29f.write(0x706, 0x0000);
			CHECK("unlockBypass", c.word(0x706) == 0xffff);
			c.program(0x706, 0x3333);
			CHECK("unlockBypass", c.word(0x706) == 0x3333);
		}
	}

	// ---- Write enable ----

	void testWriteEnable()
	{
		Chip c(0x10000, false, false, true);
		c.program(0x800, 0x0000);
		CHECK("writeEnable", c.word(0x800) == 0xffff);

		c.am29f.writeEnable(true);
		c.program(0x800, 0x1234);
		CHECK("writeEnable", c.word(0x800) == 0x1234);

		// a sequence that loses the enable halfway is gone
		c.unlock();
		c.am29f.writeEnable(false);
		c.am29f.write(c.cmd555, 0xa0);
		c.am29f.writeEnable(true);
		c.am29f.write(0x802, 0x0000);
		CHECK("writeEnable", c.word(0x802) == 0xffff);
	}
}

int main()
{
	baseLib::disableErrorDialogs();

	testProgramWord();
	testProgramByte();
	testProgramOnlyClearsBits();
	testProgramDataThatLooksLikeACommand();
	testPlainWritesDoNotChangeMemory();
	testProgramOutOfRangeIsIgnored();
	testResetAbortsASequence();
	testWrongCycleAbortsASequence();
	testJedecCommandAddresses();
	testSectorErase();
	testChipErase();
	testSectorMaps();
	testEraseBeyondTheChipIsRefused();
	testAutoselectByteMode();
	testAutoselectWordMode();
	testUnlockBypass();
	testWriteEnable();

	printf("%d of %d checks failed\n", g_failures, g_checks);
	return g_failures ? 1 : 0;
}
