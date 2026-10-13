// The presets of the MT-32 dialect's boards, from images laid out like their ROMs. With a ROM folder as argument or
// in EMU88_MT32_TEST_ROM_DIR, also every preset timbre of the LA boards against what the emulated board loads.
#include "88lib/boards/laBoard.h"
#include "88lib/mt32/params.h"
#include "88lib/mt32/presets.h"
#include "88lib/rom/romloader.h"
#include "baseLib/os.h"
#include "cpu/common/test_util.hpp"
#include "synthLib/romLoader.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace emu88Lib;
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

		PresetCatalog names;
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
		PresetCatalog broken;
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

		PresetCatalog names;
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

		PresetCatalog names;
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

	// The compressed ROMs leave a muted partial out, the board loads the one before it in its place
	void compressedTimbre()
	{
		std::vector<uint8_t> rom(0x10000, 0xff);
		putTable(rom, 0x8000, 128, 0x3000, 0x80, 0x8000, "Timbre%03d");
		putTable(rom, 0x8500, 64, 0x8a00, 0x10, 0, "Rhythm%02d");
		// a07 plays partials 1 and 3 only: the ROM has the two of them, one after the other
		const size_t timbre = 0xb000 + 6 * 0x80;
		rom[timbre + 0x0c] = 0x05;
		for(size_t i = 0; i < g_partialSize; ++i)
		{
			rom[timbre + g_timbreCommonSize + i] = 0x11;
			rom[timbre + g_timbreCommonSize + g_partialSize + i] = 0x33;
		}

		PresetCatalog presets;
		CHECK(presets.readLaControl(rom));
		const auto* data = presets.timbreData(0, 6);
		CHECK(data != nullptr);
		if(!data)
			return;
		CHECK_EQ((*data)[0x0c], 0x05);
		for(size_t p = 0; p < g_partials; ++p)
		{
			const auto at = g_timbreCommonSize + p * g_partialSize;
			CHECK_EQ((*data)[at], p < 2 ? 0x11 : 0x33);
			CHECK_EQ((*data)[at + g_partialSize - 1], p < 2 ? 0x11 : 0x33);
		}
	}

	// The presets from the ROM make the timbre temps of the parts that play them known, and stay over a reset
	void presetsInMemory()
	{
		std::vector<uint8_t> rom(0x10000, 0xff);
		putTable(rom, 0x8000, 128, 0x3000, 0x80, 0x8000, "Timbre%03d");
		putTable(rom, 0x8500, 64, 0x8a00, 0x10, 0, "Rhythm%02d");
		PresetCatalog presets;
		CHECK(presets.readLaControl(rom));
		CHECK_EQ(presets.allTimbres().size(), size_t(192));

		Memory memory(DeviceModel::Cm32l);
		const auto source = memory.timbreSource(0);
		CHECK(!memory.entryKnown(Area::TimbreTemp, 0));
		for(const auto& [s, data] : presets.allTimbres())
			memory.addPreset(s, data);
		CHECK(memory.entryKnown(Area::TimbreTemp, 0));
		const auto* expected = presets.timbreData(source.group, source.number);
		CHECK(expected != nullptr);
		if(expected)
			CHECK_EQ(memory.value(Area::TimbreTemp, 0, 5), (*expected)[5]);

		memory.reset();
		CHECK(memory.entryKnown(Area::TimbreTemp, 0));
	}

	// What a structure makes of a pair of partials, and back
	void structures()
	{
		CHECK(!structure(0).pcm[0] && !structure(0).pcm[1]);
		CHECK(structure(2).pcm[0] && !structure(2).pcm[1]);
		CHECK(!structure(4).pcm[0] && structure(4).pcm[1]);
		CHECK(structure(12).pcm[0] && structure(12).pcm[1] && structure(12).mix == StructureMix::Ring);
		CHECK(structure(7).mix == StructureMix::Stereo);
		for(uint8_t v = 0; v < 13; ++v)
		{
			const auto s = structure(v);
			uint8_t back = 0xff;
			CHECK(structureValue(s.mix, s.pcm[0], s.pcm[1], back));
			CHECK_EQ(back, v);
		}
		uint8_t value = 0;
		// No plain mix of a synth first partial with a PCM second, no stereo pair of two kinds
		CHECK(!structureValue(StructureMix::Mix, false, true, value));
		CHECK(!structureValue(StructureMix::Stereo, true, false, value));
		CHECK(structureName(3, 1) == "(P1 + S2 \u2192 R) + P1");
		CHECK(structureName(9, 3) == "(S3 + S4) \u2192 R");
		CHECK(std::string(pcmWaveName(0, 0)) == "Ac. Bass Drum");
		CHECK(std::string(pcmWaveName(1, 127)) == "Dog Bark");
	}

	// ---- against the boards

	constexpr uint32_t g_boardRate = 32000;

	struct Driver
	{
		LaBoard& board;
		std::vector<synthLib::SMidiEvent> out;

		void run(const uint32_t _samples)
		{
			for(uint32_t i = 0; i < _samples; ++i)
			{
				board.renderSample();
				if((i & 255) == 255)
					board.readMidiOut(out);
			}
			board.readMidiOut(out);
		}
		// The time a message takes on the wire, and room for the firmware to take it, which the MT-32 1.x needs
		// four times of
		void send(const Message& _m, const bool _old)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex.assign(_m.begin(), _m.end());
			board.addMidiEvent(e);
			run(static_cast<uint32_t>(_m.size() * (11 + g_boardRate / 1000) + g_boardRate / 25) * (_old ? 4 : 1));
		}
	};

	// Each preset selected on part 1, its timbre temp read back with a data request
	void againstBoard(const DeviceModel _model)
	{
		const auto roms = RomLoader::findLaRomSet(toLaModel(_model));
		LaBoard board(roms);
		if(!board.isValid())
		{
			std::printf("%s: no ROMs, skipped\n", getDeviceProfile(_model).displayName);
			return;
		}
		PresetCatalog presets;
		CHECK(presets.readLaControl(roms.control));
		const bool old = laFirmware(_model) == LaFirmware::Old;

		Driver d{board};
		d.run(g_boardRate * 3);
		size_t compared = 0, differ = 0;
		for(const uint8_t group : {0, 1, 3})
		{
			for(uint8_t number = 0; number < 64; ++number)
			{
				const auto* expected = presets.timbreData(group, number);
				if(!expected)
					continue;
				d.send(dt1(g_patchTemp, std::vector<uint8_t>{group, number}), old);
				d.out.clear();
				d.send(rq1(g_timbreTemp, g_timbreSize), old);
				// Until the answer is in, or the board has been quiet for half a second
				std::vector<int> got(g_timbreSize, -1);
				size_t received = 0;
				for(uint32_t quiet = 0; received < g_timbreSize && quiet < g_boardRate / 2;)
				{
					const auto before = d.out.size();
					d.run(320);
					quiet = d.out.size() == before ? quiet + 320 : 0;
					received = 0;
					for(const auto& e : d.out)
					{
						Dt1 answer;
						if(e.sysex.empty() || !parseDt1(e.sysex.data(), e.sysex.size(), answer))
							continue;
						received += answer.size;
						for(size_t i = 0; i < answer.size; ++i)
						{
							const auto at = toLinear(answer.address) + i - toLinear(g_timbreTemp);
							if(at < got.size())
								got[at] = answer.data[i];
						}
					}
				}
				++compared;
				for(size_t i = 0; i < g_timbreSize; ++i)
				{
					if(got[i] == (*expected)[i])
						continue;
					if(differ < 5)
					{
						std::printf("%s: %c%02d differs at %02zX: board %d, ROM %d\n",
							getDeviceProfile(_model).displayName, "abir"[group], number + 1, i, got[i], (*expected)[i]);
					}
					++differ;
					break;
				}
			}
		}
		std::printf("%s: %zu preset timbres compared, %zu differ\n", getDeviceProfile(_model).displayName, compared,
			differ);
		CHECK(compared > 0);
		CHECK_EQ(differ, size_t(0));
	}
}

int main(int _argc, char** _argv)
{
	newLayout();
	oldLayout();
	compressedTimbre();
	presetsInMemory();
	structures();
	pcmTones();

	baseLib::disableErrorDialogs();
	if(const char* folder = _argc > 1 ? _argv[1] : std::getenv("EMU88_MT32_TEST_ROM_DIR"))
	{
		synthLib::RomLoader::setSearchPath(folder);
		for(const auto model : {DeviceModel::Mt32Old, DeviceModel::Mt32New, DeviceModel::Cm32l})
			againstBoard(model);
	}
	return finish("mt32Presets");
}
