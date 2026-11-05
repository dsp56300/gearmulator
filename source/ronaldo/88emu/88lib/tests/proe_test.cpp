#include "88lib/boards/proe.h"
#include "common/test_util.hpp"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

// Boots the PRO-E from its ROMs with the CM-32L standing in for its sound board, starts the
// arranger and plays a chord into it. Needs the images, in the folder named by the first
// argument or EMU88_PROE_TEST_ROM_DIR: MAME's roland_proe_v100.ic4, .ic6, .ic7, .ic17 and .ic18,
// and the CM-32L's under munt's names, cm32l_control.rom and cm32l_pcm.rom, with the reverb
// microcode r15179917.ic19.bin. Skipped without them.
//
// For bring-up: PROE_LED_PROBE=<switch>,<switch>,... presses those ProEButtons in turn from a fresh boot and
// lists the LEDs lit after each (* = blinking), as LED row.bit; with PROE_LED_BEAT set as well it prints the
// beat lamps every 50 ms, stopped and then playing. PROE_RAM_WATCH=<hex offset> prints that byte of RAM bank 0
// every 50 ms from power-on. PROE_SYSEX_FILE=<file> sends one SysEx message per line (hex) 0.1 s apart and
// prints the LCD as it changes and what went out on the link, with the firmware's power-on protection
// (PROE_UNPROTECTED: without); PROE_SYSEX_BUFFER adds the receive state and the MIDI IN ring after each.
namespace
{
	using emu88Lib::ProE;
	using emu88Lib::ProEButton;

	std::vector<uint8_t> load(const std::string& _path)
	{
		std::vector<uint8_t> data;
		baseLib::filesystem::readFile(data, _path);
		return data;
	}

	std::string lcdLine(const ProE& _proe, const unsigned _line)
	{
		std::string text;
		for(unsigned column = 0; column < 16; ++column)
			text += static_cast<char>(_proe.lcd().getVisibleCharacter(_line, column));
		return text;
	}

	uint32_t bit(const ProEButton _button) { return uint32_t{1} << static_cast<uint8_t>(_button); }

	int32_t render(ProE& _proe, const double _seconds)
	{
		int32_t peak = 0;
		for(auto samples = static_cast<uint32_t>(_seconds * ProE::SampleRate); samples > 0; --samples)
		{
			const auto frame = _proe.renderSample();
			peak = std::max({peak, std::abs(frame.first), std::abs(frame.second)});
		}
		return peak;
	}
}

int main(const int _argc, char** _argv)
{
	const char* env = std::getenv("EMU88_PROE_TEST_ROM_DIR");
	const std::string folder = baseLib::filesystem::validatePath(_argc > 1 ? _argv[1] : env ? env : "");

	emu88Lib::ProERomSet roms;
	roms.program = load(folder + "roland_proe_v100.ic4");
	const char* patterns[] = {"ic6", "ic7", "ic17", "ic18"};
	for(size_t i = 0; i < roms.patterns.size(); ++i)
		roms.patterns[i] = load(folder + "roland_proe_v100." + patterns[i]);
	emu88Lib::LaRomSet sound;
	sound.model = emu88Lib::LaModel::Cm32l;
	sound.control = load(folder + "cm32l_control.rom");
	sound.wave = load(folder + "cm32l_pcm.rom");
	sound.reverb = load(folder + "r15179917.ic19.bin");
	if(!roms.isValid() || !sound.isValid())
	{
		std::printf("proe_test: ROMs not found, skipped\n");
		return 0;
	}

	ProE proe(roms, sound);
	CHECK(proe.isValid());

	if(const char* file = std::getenv("PROE_SYSEX_FILE"))
	{
		// Bring-up: one SysEx message per line (hex, F0 to F7), sent 0.1 s apart after power-on; the
		// LCD whenever it changes.
		if(std::getenv("PROE_UNPROTECTED") == nullptr)
		{
			proe.setMemoryProtectAtPowerOn(true);
			proe.reset();
		}
		render(proe, 4.0);
		std::string link;
		proe.setLinkMonitor([&](const uint8_t _b) { char hex[4]; std::snprintf(hex, sizeof(hex), "%02x ", _b); link += hex; });
		std::vector<uint8_t> text;
		baseLib::filesystem::readFile(text, file);
		std::string line, last;
		int index = 0;
		for(size_t i = 0; i <= text.size(); ++i)
		{
			if(i < text.size() && text[i] != '\n') { line += static_cast<char>(text[i]); continue; }
			if(line.empty()) continue;
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			for(size_t c = 0; c + 1 < line.size(); c += 2)
				event.sysex.push_back(static_cast<uint8_t>(std::stoul(line.substr(c, 2), nullptr, 16)));
			proe.addMidiEvent(event);
			render(proe, 0.1);
			const auto lcd = lcdLine(proe, 0) + "|" + lcdLine(proe, 1);
			if(lcd != last || std::getenv("PROE_SYSEX_BUFFER"))
				std::printf("%3d %-24.24s |%s|\n", index, line.c_str(), lcd.c_str());
			if(!link.empty())
				std::printf("    link: %.240s\n", link.c_str());
			link.clear();
			if(std::getenv("PROE_SYSEX_BUFFER"))
			{
				// The receive state: 268A-2696 of RAM bank 0, and the MIDI IN ring at 2000 with its
				// write / read indices (HC11 RAM 00 / 01).
				for(size_t a = 0x68a; a <= 0x696; ++a)
					std::printf(" %02x", proe.ram()[a]);
				const auto* internal = proe.machine().bus().mem();
				std::printf("   ring w=%02x r=%02x:", internal[0], internal[1]);
				for(size_t a = 0; a < 40; ++a)
					std::printf(" %02x", proe.ram()[a]);
				std::printf("\n");
			}
			last = lcd;
			line.clear();
			++index;
		}
		return 0;
	}

	if(std::getenv("PROE_RAM_WATCH"))
	{
		// Bring-up: one byte of the banked RAM (bank 0 offset, hex) every 50 ms from power-on.
		const auto offset = std::strtoul(std::getenv("PROE_RAM_WATCH"), nullptr, 16);
		for(int i = 0; i < 80; ++i)
		{
			render(proe, 0.05);
			std::printf("%02x ", proe.ram()[offset]);
		}
		std::printf("\n");
		return 0;
	}

	if(std::getenv("PROE_LED_PROBE"))
	{
		// Bring-up: each arranger switch pressed in turn, then every LED sampled for 1.2 s: lit
		// throughout (on), part of the time (blink) or not at all. Sequence from a fresh boot.
		render(proe, 4.0);
		const auto survey = [&](const char* _what)
		{
			std::array<std::array<int, 8>, 8> count{};
			constexpr int samples = 40;
			for(int i = 0; i < samples; ++i)
			{
				render(proe, 0.03);
				for(size_t r = 0; r < 8; ++r)
					for(unsigned bit = 0; bit < 8; ++bit)
						count[r][bit] += (proe.ledRows()[r] >> bit) & 1;
			}
			std::printf("%-10s", _what);
			for(size_t r = 0; r < 8; ++r)
				for(unsigned bit = 0; bit < 8; ++bit)
					if(count[r][bit])
						std::printf(" %zu.%u%s", r, bit, count[r][bit] == samples ? "" : "*");
			std::printf("   |%s|%s|\n", lcdLine(proe, 0).c_str(), lcdLine(proe, 1).c_str());
		};
		survey("boot");
		const char* names[] = {"START", "INTRO", "FILLORIG", "FILLVARI", "SYNCSTART", "SYNCSTOP", "ARRHOLD",
		                       "ADVARR", "VARIATION", "TAP", "FADEIN", "FADEOUT", "STYLE", "GROUP",
		                       "BANK1", "BANK2", "BANK3", "BANK4", "BANK5", "BANK6", "BANK7", "BANK8",
		                       "NUM1", "NUM2", "NUM3", "NUM4", "NUM5", "NUM6", "NUM7", "NUM8"};
		if(std::getenv("PROE_LED_BEAT"))
		{
			// Row 6's low nibble every 50 ms, stopped and then playing at 110 bpm (a beat is 545 ms).
			for(int phase = 0; phase < 2; ++phase)
			{
				if(phase)
				{
					proe.setButtons(1);
					render(proe, 0.05);
					proe.setButtons(0);
				}
				for(int i = 0; i < 50; ++i)
				{
					render(proe, 0.05);
					std::printf("%x", proe.ledRows()[6] & 15);
				}
				std::printf("\n");
			}
			return 0;
		}
		const char* order = std::getenv("PROE_LED_PROBE");
		for(const char* p = order; *p; )
		{
			const int b = std::atoi(p);
			proe.setButtons(uint32_t{1} << b);
			render(proe, 0.15);
			proe.setButtons(0);
			survey(names[b]);
			while(*p && *p != ',') ++p;
			if(*p) ++p;
		}
		return 0;
	}

	// Power-on: the banner, then the main screen with the first style and its tempo.
	render(proe, 4.0);
	std::printf("LCD |%s|%s|\n", lcdLine(proe, 0).c_str(), lcdLine(proe, 1).c_str());
	CHECK(lcdLine(proe, 0).find("FUNK2") != std::string::npos);
	// The power-on setup of the sound board went over the link.
	CHECK(proe.linkBytes() > 100);
	CHECK(proe.machine().cpu().illegal_instructions() == 0);
	// ADVANCED ARRANGER is on from power-on and VARIATION off; the latter toggles.
	const auto led = [&](const emu88Lib::ProELed _led) { return (proe.leds() >> static_cast<uint8_t>(_led)) & 1; };
	CHECK(led(emu88Lib::ProELed::AdvancedArranger));
	CHECK(!led(emu88Lib::ProELed::Variation));
	CHECK(!led(emu88Lib::ProELed::StartStop));
	proe.setButtons(bit(ProEButton::Variation));
	render(proe, 0.2);
	proe.setButtons(0);
	render(proe, 0.2);
	CHECK(led(emu88Lib::ProELed::Variation));

	// START/STOP, then a C major chord on channel 1, the arranger's chord input.
	proe.setButtons(bit(ProEButton::StartStop));
	render(proe, 0.2);
	proe.setButtons(0);
	for(const uint8_t note : {0x30, 0x34, 0x37})
		proe.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x90, note, 0x64));
	const auto linkBefore = proe.linkBytes();
	const auto peak = render(proe, 3.0);
	std::printf("LCD |%s|%s|, link %llu bytes, peak %d\n", lcdLine(proe, 0).c_str(), lcdLine(proe, 1).c_str(),
		static_cast<unsigned long long>(proe.linkBytes()), peak);
	CHECK(lcdLine(proe, 1).find("C Ma") != std::string::npos);
	CHECK(led(emu88Lib::ProELed::StartStop));
	// The beat lamps step through the bar: each is lit for a beat of 545 ms at 110 bpm.
	uint16_t beats = 0;
	for(int i = 0; i < 50; ++i)
	{
		render(proe, 0.05);
		beats |= proe.leds() >> static_cast<uint8_t>(emu88Lib::ProELed::Beat1);
	}
	CHECK_EQ(beats & 15, 15);
	CHECK(proe.linkBytes() > linkBefore + 200);
	CHECK(peak > 1000);

	// INTERNAL MEMORY protection: off from power-on here, so the PRO-E's own DT1 (model 22h) is taken;
	// with the firmware's power-on default it is refused with INTERNAL MEMORY PROTECTED.
	CHECK_EQ(proe.ram()[0x452] & 0x80, 0);
	const auto dt1 = [](ProE& _board)
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
		event.sysex = {0xf0, 0x41, 0x10, 0x22, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf7};
		_board.addMidiEvent(event);
		render(_board, 0.3);
	};
	const auto protectedShown = [](const ProE& _board)
	{
		return (lcdLine(_board, 0) + lcdLine(_board, 1)).find("PROTECTED") != std::string::npos;
	};
	dt1(proe);
	CHECK(!protectedShown(proe));
	{
		ProE guarded(roms, sound);
		guarded.setMemoryProtectAtPowerOn(true);
		guarded.reset();
		render(guarded, 4.0);
		CHECK_EQ(guarded.ram()[0x452] & 0x80, 0x80);
		dt1(guarded);
		std::printf("protected: LCD |%s|%s|\n", lcdLine(guarded, 0).c_str(), lcdLine(guarded, 1).c_str());
		CHECK(protectedShown(guarded));
	}

	return test::finish("proe_test");
}
