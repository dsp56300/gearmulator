#include "88lib/boards/mv30.h"
#include "88lib/hardwareDevice.h"
#include "88lib/rom/romloader.h"
#include "common/test_util.hpp"

#include "baseLib/filesystem.h"
#include "synthLib/romLoader.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

// Boots the MV-30 board from its real boot ROM and system disk. Needs the images, in the
// folder named by the first argument or EMU88_MV30_TEST_ROM_DIR, under MAME's names:
// mv30.bin, roland_d70_waverom-a.bin .. -f.bin, t6963c_0101.bin (optional) and one 720 KiB
// system disk image (*.dsk, *.img or *.out). Skipped without them.
//
// For bring-up: MV30_TRACE=<seconds> first boots a board with the FDC's command trace on,
// MV30_LCD_DUMP=<file> writes the final screen (240 x 64 bytes, 1 = lit) and
// MV30_WAV=<file> renders a short piano phrase, MV30_SEND="c0 05,b0 07 40" sends MIDI before the screen is
// printed and MV30_RAM_DUMP=<file> writes the 512 KiB of RAM at the end.
namespace
{
	using emu88Lib::Mv30;
	using emu88Lib::Mv30RomSet;

	std::vector<uint8_t> load(const std::string& _path)
	{
		std::vector<uint8_t> data;
		baseLib::filesystem::readFile(data, _path);
		return data;
	}

	Mv30RomSet findRoms(const std::string& _folder)
	{
		const auto folder = baseLib::filesystem::validatePath(_folder);
		Mv30RomSet roms;
		roms.boot = load(folder + "mv30.bin");
		for (size_t i = 0; i < roms.waves.size(); ++i)
			roms.waves[i] = load(folder + "roland_d70_waverom-" + std::string(1, char('a' + i)) + ".bin");
		roms.cgRom = load(folder + "t6963c_0101.bin");
		std::vector<std::string> files;
		baseLib::filesystem::getDirectoryEntries(files, folder);
		std::sort(files.begin(), files.end());
		for (const auto& file : files)
		{
			const auto extension = baseLib::filesystem::getExtension(file);
			if (extension != ".dsk" && extension != ".img" && extension != ".out")
				continue;
			auto disk = load(file);
			if (disk.size() != Mv30RomSet::SystemDiskSize)
				continue;
			roms.systemDisk = std::move(disk);
			std::printf("system disk: %s\n", baseLib::filesystem::getFilenameWithoutPath(file).c_str());
			break;
		}
		return roms;
	}

	void printLcd(const Mv30& _board)
	{
		std::vector<uint8_t> pixels;
		_board.lcd().renderMono(pixels);
		const auto width = _board.lcd().width();
		const auto height = _board.lcd().height();
		for (uint32_t y = 0; y < height; y += 2)
		{
			std::string line;
			for (uint32_t x = 0; x < width; x += 2)
			{
				const bool on = pixels[y * width + x] || pixels[y * width + x + 1] ||
					pixels[(y + 1) * width + x] || pixels[(y + 1) * width + x + 1];
				line.push_back(on ? '#' : '.');
			}
			std::printf("%s\n", line.c_str());
		}
	}
}

int main(const int _argc, char** _argv)
{
	const char* folder = _argc > 1 ? _argv[1] : std::getenv("EMU88_MV30_TEST_ROM_DIR");
	if (!folder)
	{
		std::printf("mv30: no ROM folder, skipped\n");
		return 0;
	}
	const auto roms = findRoms(folder);
	if (!roms.isValid())
	{
		std::printf("mv30: ROMs incomplete in %s, skipped\n", folder);
		return 0;
	}

	// The same set through the ROM scanner, which recognizes the images by content.
	{
		synthLib::RomLoader::setSearchPath(folder);
		const auto inventory = emu88Lib::RomLoader::rescan();
		CHECK(inventory.isComplete(emu88Lib::RomDevice::Mv30));
		const auto scanned = emu88Lib::RomLoader::findMv30RomSet();
		CHECK(scanned.isValid());
		CHECK(scanned.boot == roms.boot && scanned.waves == roms.waves && scanned.cgRom == roms.cgRom);

		// The front panel through the player's path: SYSTEM opens the system screen, PLAY goes back
		synthLib::DeviceCreateParams params;
		params.customData = static_cast<uint32_t>(emu88Lib::DeviceModel::Mv30);
		emu88Lib::HardwareDevice device(params);
		CHECK(device.isValid());
		std::array<float, 512> left{}, right{};
		synthLib::TAudioOutputs audio{};
		audio[0] = left.data();
		audio[1] = right.data();
		std::vector<synthLib::SMidiEvent> output;
		const auto run = [&](const float _seconds)
		{
			for (int i = 0; i < int(_seconds * Mv30::SampleRate / 512); ++i)
				device.process({}, audio, 512, {}, output);
		};
		const auto screen = [&] { return device.displaySnapshot().screens[0].mono; };
		const auto press = [&](const emu88Lib::Mv30Button _button)
		{
			device.setMv30Panel(uint64_t{1} << static_cast<uint8_t>(_button), false);
			run(0.1f);
			device.setMv30Panel(0, false);
			run(0.5f);
		};
		run(0.5f);
		const auto play = screen();
		CHECK(!play.empty());
		press(emu88Lib::Mv30Button::System);
		const auto system = screen();
		CHECK(system != play);
		press(emu88Lib::Mv30Button::Play);
		const auto back = screen();
		CHECK(back == play);
	}

	if (std::getenv("MV30_TRACE"))
	{
		Mv30 traced(roms);
		traced.fdc().setTraceHook([&](const std::string& _line)
		{
			std::printf("%8.3f pc %04x %s\n", double(traced.samplesRendered()) / Mv30::SampleRate, traced.machine().cpu().regs().pc, _line.c_str());
		});
		traced.reset(false);
		traced.bootOffline(std::strtof(std::getenv("MV30_TRACE"), nullptr));
	}

	// A PCM card in slot A: the OS reads the card's tone list while it starts, so its RAM
	// differs from a boot without one.
	if (const char* cardPath = std::getenv("MV30_CARD"))
	{
		const auto card = load(cardPath);
		Mv30 withCard(roms, card);
		Mv30 without(roms);
		CHECK(withCard.hasCard());
		CHECK(withCard.booted());
		size_t differing = 0;
		for (size_t i = 0; i < Mv30::RamSize; ++i)
			differing += withCard.ram()[i] != without.ram()[i];
		std::printf("card: %zu RAM bytes differ after boot\n", differing);
		if (const char* dump = std::getenv("MV30_RAM_DUMP"))
		{
			baseLib::filesystem::writeFile(std::string(dump) + ".card", withCard.ram());
			baseLib::filesystem::writeFile(std::string(dump) + ".empty", without.ram());
		}
		CHECK(differing > 0);
	}

	const auto start = std::chrono::steady_clock::now();
	Mv30 board(roms);
	const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	CHECK(board.isValid());
	CHECK(board.booted());
	std::printf("disk \"%s\", %u sectors read, %.2f s emulated in %.2f s\n", board.systemDisk().label().c_str(),
		board.fdc().sectorsRead(), double(board.samplesRendered()) / Mv30::SampleRate, seconds);
	std::printf("pc %04x, banks i %03x %03x %03x %03x d %03x %03x %03x %03x\n", board.machine().cpu().regs().pc,
		board.gate().bank(0), board.gate().bank(1), board.gate().bank(2), board.gate().bank(3),
		board.gate().bank(4), board.gate().bank(5), board.gate().bank(6), board.gate().bank(7));
	// Part 1 (A.Piano 1, MIDI channel 1 by default) plays a note.
	{
		int32_t silencePeak = 0, notePeak = 0;
		for (uint32_t i = 0; i < Mv30::SampleRate / 4; ++i)
		{
			const auto [l, r] = board.renderSample();
			silencePeak = std::max({silencePeak, std::abs(l), std::abs(r)});
		}
		board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x90, 60, 100));
		for (uint32_t i = 0; i < Mv30::SampleRate / 2; ++i)
		{
			const auto [l, r] = board.renderSample();
			notePeak = std::max({notePeak, std::abs(l), std::abs(r)});
		}
		board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x80, 60, 0));
		std::printf("peak before the note %d, with it %d (full scale %d), output %s\n", silencePeak, notePeak, 1 << 23,
			board.outputMuted() ? "muted" : "on");
		CHECK(notePeak > 0x10000);
	}

	// The part settings in the OS's song memory: the power-on song, then what MIDI changes of it.
	{
		emu88Lib::Mv30Parts parts;
		board.readParts(parts);
		CHECK(parts.valid);
		CHECK_EQ(int(parts.parts[0].timbre), 0);
		CHECK_EQ(int(parts.parts[0].channel), 0);
		CHECK_EQ(int(parts.parts[0].level), 127);
		CHECK_EQ(int(parts.parts[7].timbre), int(emu88Lib::Mv30Parts::RhythmTimbre));
		CHECK_EQ(int(parts.parts[7].channel), 9);
		CHECK(std::string(parts.timbreNames[0].data(), 9) == "A.Piano 1");
		const auto send = [&](const uint8_t _a, const uint8_t _b, const uint8_t _c)
		{
			board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, _a, _b, _c));
			for (uint32_t i = 0; i < Mv30::SampleRate / 20; ++i)
				board.renderSample();
		};
		send(0xc1, 0x05, 0);
		send(0xb1, 0x07, 0x40);
		send(0xb1, 0x0a, emu88Lib::Mv30Parts::panController(-3));
		board.readParts(parts);
		CHECK_EQ(int(parts.parts[1].timbre), 5);
		CHECK_EQ(int(parts.parts[1].level), 0x40);
		CHECK_EQ(int(parts.parts[1].pan), -3);
		for (int8_t pan = -7; pan <= 7; ++pan)
		{
			send(0xb2, 0x0a, emu88Lib::Mv30Parts::panController(pan));
			board.readParts(parts);
			CHECK_EQ(int(parts.parts[2].pan), int(pan));
		}
		// Back to the power-on settings for what follows
		send(0xc1, 0x01, 0);
		send(0xb1, 0x07, 0x7f);
		send(0xb1, 0x0a, 0x40);
		send(0xb2, 0x0a, 0x40);
	}

	// A short phrase as a 16-bit stereo WAV, to listen to.
	if (const char* wav = std::getenv("MV30_WAV"))
	{
		std::vector<int16_t> samples;
		auto render = [&](const float _seconds)
		{
			for (uint32_t i = 0; i < uint32_t(_seconds * Mv30::SampleRate); ++i)
			{
				const auto [l, r] = board.renderSample();
				samples.push_back(int16_t(std::clamp(l >> 8, -32768, 32767)));
				samples.push_back(int16_t(std::clamp(r >> 8, -32768, 32767)));
			}
		};
		render(1.0f);
		for (const uint8_t note : {60, 64, 67, 72, 67, 64})
		{
			board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x90, note, 100));
			render(0.3f);
			board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x80, note, 0));
		}
		for (const uint8_t note : {48, 60, 64, 67})
			board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x90, note, 100));
		render(1.5f);
		for (const uint8_t note : {48, 60, 64, 67})
			board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x80, note, 0));
		render(1.5f);
		const auto bytes = uint32_t(samples.size() * 2);
		std::vector<uint8_t> file;
		auto put = [&](const uint32_t _v, const int _n) { for (int i = 0; i < _n; ++i) file.push_back(uint8_t(_v >> (8 * i))); };
		file.insert(file.end(), {'R', 'I', 'F', 'F'}); put(36 + bytes, 4);
		file.insert(file.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); put(16, 4); put(1, 2); put(2, 2);
		put(Mv30::SampleRate, 4); put(Mv30::SampleRate * 4, 4); put(4, 2); put(16, 2);
		file.insert(file.end(), {'d', 'a', 't', 'a'}); put(bytes, 4);
		const auto* raw = reinterpret_cast<const uint8_t*>(samples.data());
		file.insert(file.end(), raw, raw + bytes);
		baseLib::filesystem::writeFile(wav, file);
	}

	if (const char* send = std::getenv("MV30_SEND"))
	{
		// Comma-separated messages in hex ("c0 05,b0 07 40"), sent after the checks, each
		// followed by half a second.
		std::string text(send);
		size_t pos = 0;
		while (pos < text.size())
		{
			const auto end = text.find(',', pos);
			const auto part = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
			uint8_t bytes[3]{};
			size_t count = 0;
			for (const char* c = part.c_str(); *c && count < 3;)
			{
				char* next = nullptr;
				bytes[count++] = uint8_t(std::strtoul(c, &next, 16));
				if (next == c) break;
				c = next;
			}
			board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, bytes[0], bytes[1], bytes[2]));
			for (uint32_t i = 0; i < Mv30::SampleRate / 2; ++i)
				board.renderSample();
			pos = end == std::string::npos ? text.size() : end + 1;
		}
	}
	printLcd(board);
	if (const char* dump = std::getenv("MV30_RAM_DUMP"))
		baseLib::filesystem::writeFile(dump, board.ram());
	if (const char* dump = std::getenv("MV30_LCD_DUMP"))
	{
		std::vector<uint8_t> pixels;
		board.lcd().renderMono(pixels);
		baseLib::filesystem::writeFile(dump, pixels);
	}
	return test::finish("mv30");
}
