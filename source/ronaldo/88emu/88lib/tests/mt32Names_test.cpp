// The preset names of the MT-32 dialect's boards, from images laid out like their ROMs. No ROMs.
#include "88lib/mt32/names.h"
#include "cpu/common/test_util.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace emu88Lib::mt32;
using namespace test;

namespace
{
	// A timbre's common block: the name, then structures, partial mute and envelope mode in range
	void putTimbre(std::vector<uint8_t>& _rom, const size_t _offset, const std::string& _name)
	{
		std::string name = _name;
		name.resize(10, ' ');
		for(size_t i = 0; i < 10; ++i)
			_rom[_offset + i] = static_cast<uint8_t>(name[i]);
		_rom[_offset + 10] = 3;
		_rom[_offset + 11] = 12;
		_rom[_offset + 12] = 15;
		_rom[_offset + 13] = 1;
	}

	void putTable(std::vector<uint8_t>& _rom, const size_t _table, const size_t _count, const uint32_t _first,
		const uint32_t _stride, const uint32_t _base, const char* _format, const size_t _numberOffset = 0)
	{
		for(size_t i = 0; i < _count; ++i)
		{
			const auto pointer = _first + i * _stride;
			_rom[_table + i * 2] = static_cast<uint8_t>(pointer);
			_rom[_table + i * 2 + 1] = static_cast<uint8_t>(pointer >> 8);
			char name[16];
			std::snprintf(name, sizeof(name), _format, static_cast<int>(i + _numberOffset));
			putTimbre(_rom, pointer + _base, name);
		}
	}

	void newLayout()
	{
		// The CM-32L's: groups a and b in one table at 8000H, pointing 8000H below where they are, the rhythm
		// timbres at 8500H
		std::vector<uint8_t> rom(0x10000, 0xff);
		putTable(rom, 0x8000, 128, 0x3000, 0x20, 0x8000, "Timbre%03d");
		putTable(rom, 0x8500, 64, 0x8a00, 0x10, 0, "Rhythm%02d");

		NameCatalog names;
		CHECK(names.readLaControl(rom));
		CHECK(names.hasTimbres());
		CHECK(names.timbre(0, 0) == "Timbre000");
		CHECK(names.timbre(0, 63) == "Timbre063");
		CHECK(names.timbre(1, 0) == "Timbre064");
		CHECK(names.timbre(1, 63) == "Timbre127");
		CHECK(names.timbre(3, 5) == "Rhythm05");
		CHECK(names.timbre(3, 63) == "Rhythm63");
		// The timbre memory is the board's, and there are no more than 64 in a group
		CHECK(names.timbre(2, 0).empty());
		CHECK(names.timbre(0, 64).empty());

		// A table that is not one of timbres is no layout
		rom[0xb000 + 7 * 0x20 + 10] = 13;
		NameCatalog broken;
		CHECK(!broken.readLaControl(rom));
		CHECK(!broken.hasTimbres());
	}

	void oldLayout()
	{
		// The MT-32 1.x: groups a and b in 16 KiB pages, each with its table first, and 30 rhythm timbres at 3200H
		std::vector<uint8_t> rom(0x10000, 0xff);
		putTable(rom, 0x8000, 64, 0x8080, 0x20, 0, "A%02d");
		putTable(rom, 0xc000, 64, 0x8080, 0x20, 0x4000, "B%02d");
		putTable(rom, 0x3200, 30, 0x3240, 0x10, 0, "R%02d");

		NameCatalog names;
		CHECK(names.readLaControl(rom));
		CHECK(names.timbre(0, 2) == "A02");
		CHECK(names.timbre(1, 2) == "B02");
		CHECK(names.timbre(3, 29) == "R29");
		CHECK(names.timbre(3, 30).empty());
	}

	void pcmTones()
	{
		// The tone table of a wave image: 128 places of 50H bytes from 1000H, a name first
		std::vector<uint8_t> image(0x1000 + 128 * 0x50, 0);
		const auto put = [&](const size_t _tone, const std::string& _name)
		{
			for(size_t i = 0; i < 10; ++i)
				image[0x1000 + _tone * 0x50 + i] = static_cast<uint8_t>(i < _name.size() ? _name[i] : ' ');
		};
		put(0, "A.PIANO 1");
		put(1, "TP / TRB 1");
		put(2, "");

		NameCatalog names;
		CHECK(!names.hasPcmTones());
		names.readPcmTones(0, image.data(), image.size());
		CHECK(names.hasPcmTones());
		CHECK(names.pcmTone(0, 0) == "A.PIANO 1");
		CHECK(names.pcmTone(0, 1) == "TP / TRB 1");
		// A blank place, one that holds no text, and no card
		CHECK(names.pcmTone(0, 2).empty());
		CHECK(names.pcmTone(0, 3).empty());
		CHECK(names.pcmTone(1, 0).empty());

		names.readPcmTones(1, image.data(), image.size());
		CHECK(names.pcmTone(1, 1) == "TP / TRB 1");
	}
}

int main()
{
	newLayout();
	oldLayout();
	pcmTones();
	return finish("mt32Names");
}
