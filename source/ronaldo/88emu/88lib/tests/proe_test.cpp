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
// PROE_SEQ=<row>.<bit>,... presses switches of the matrix in turn (a+b together), with the style card
// PROE_CARD in the slot, and prints the LCD and the style state after each; PROE_PC_SCAN=<hex> lists the
// switches that make the firmware run the code at that address (PROE_PC_SCAN_STYLE: after STYLE).
// EMU88_PROE_TEST_CARD=<file> names a style card (a TN-SC1 dump) to check the card slot with.
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

	if(const char* sequence = std::getenv("PROE_SEQ"))
	{
		// Bring-up: boots with the style card PROE_CARD (if any), then presses the switches of the matrix
		// listed as <row>.<bit>, comma separated; <row>.<bit>+<row>.<bit> presses them together. After
		// each step the LCD, the style number (HC11 RAM 72), 2409-240B and the card state (AF).
		std::vector<uint8_t> card;
		if(const char* cardFile = std::getenv("PROE_CARD"))
			card = load(cardFile);
		ProE seq(roms, sound, card);
		render(seq, 4.0);
		const auto show = [&](const std::string& _what)
		{
			const auto* internal = seq.machine().bus().mem();
			std::printf("%-12s |%s|%s| 72=%02x 60=%02x 0b=%02x 2409=%02x %02x %02x AF=%02x\n", _what.c_str(),
				lcdLine(seq, 0).c_str(), lcdLine(seq, 1).c_str(), internal[0x72], internal[0x60], internal[0x0b],
				seq.ram()[0x409], seq.ram()[0x40a], seq.ram()[0x40b], internal[0xaf]);
		};
		show("boot");
		std::string step;
		for(const char* p = sequence; ; ++p)
		{
			if(*p && *p != ',') { step += *p; continue; }
			std::vector<std::pair<unsigned, unsigned>> keys;
			for(size_t i = 0; i < step.size(); )
			{
				const auto dot = step.find('.', i);
				auto end = step.find('+', i);
				if(end == std::string::npos) end = step.size();
				keys.emplace_back(std::stoul(step.substr(i, dot - i)), std::stoul(step.substr(dot + 1, end - dot - 1)));
				i = end + 1;
			}
			for(const auto& [r, b] : keys) { seq.setKey(r, b, true); render(seq, 0.1); }
			render(seq, 0.1);
			for(const auto& [r, b] : keys) seq.setKey(r, b, false);
			render(seq, 0.5);
			show(step);
			step.clear();
			if(!*p) break;
		}
		return 0;
	}

	if(const char* target = std::getenv("PROE_PC_SCAN"))
	{
		// Bring-up: which switch of the matrix makes the firmware execute the code at that address (hex)
		// within 0.3 s of being pressed, from a fresh boot each.
		const auto pc = static_cast<uint16_t>(std::strtoul(target, nullptr, 16));
		for(unsigned row = 0; row < 8; ++row)
		{
			for(unsigned b = 0; b < 11; ++b)
			{
				std::vector<uint8_t> card;
				if(const char* cardFile = std::getenv("PROE_CARD"))
					card = load(cardFile);
				ProE scan(roms, sound, card);
				render(scan, 4.0);
				// PROE_PC_SCAN_STYLE: STYLE pressed first.
				if(std::getenv("PROE_PC_SCAN_STYLE"))
				{
					scan.setKey(0, 0, true);
					render(scan, 0.15);
					scan.setKey(0, 0, false);
					render(scan, 0.5);
				}
				scan.setKey(row, b, true);
				bool hit = false;
				for(uint64_t cycles = 0; cycles < ProE::EClock * 3 / 10 && !hit; )
				{
					cycles += scan.machine().step();
					hit = scan.machine().cpu().pc() == pc;
				}
				if(hit)
					std::printf("%u.%u reaches %04x\n", row, b, pc);
			}
		}
		return 0;
	}

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

	// The style card slot: an image the firmware takes is accepted, anything else is not.
	{
		std::vector<uint8_t> e20(ProE::CardBankSize, 0xff);
		const uint8_t header[] = {'R', 'o', 'l', 'a', 'n', 'd', ' ', 'E', '-', '2', '0', ' ', 'N', 0xb1, 'Q', 0xae};
		std::copy(std::begin(header), std::end(header), e20.begin());
		CHECK(ProE::isCardImage(e20));
		auto bad = e20;
		bad[13] = 'N';
		CHECK(!ProE::isCardImage(bad));
		CHECK(!ProE::isCardImage(std::vector<uint8_t>(e20.begin(), e20.begin() + 0x4000)));
	}
	// Without a card, BANK 5 (the card's first bank) does not leave the internal styles. With one
	// (EMU88_PROE_TEST_CARD, a TN-SC1 style card), it selects the card's styles, marked C, and they play.
	{
		const auto pressKey = [](ProE& _board, const ProEButton _button)
		{
			_board.setButtons(bit(_button));
			render(_board, 0.15);
			_board.setButtons(0);
			render(_board, 0.5);
		};
		ProE empty(roms, sound);
		render(empty, 4.0);
		pressKey(empty, ProEButton::Style);
		pressKey(empty, ProEButton::Bank5);
		pressKey(empty, ProEButton::Number1);
		std::printf("no card: LCD |%s|%s|\n", lcdLine(empty, 0).c_str(), lcdLine(empty, 1).c_str());
		CHECK(lcdLine(empty, 0)[0] != 'C');

		if(const char* cardFile = std::getenv("EMU88_PROE_TEST_CARD"))
		{
			auto card = load(cardFile);
			CHECK(ProE::isCardImage(card));
			ProE withCard(roms, sound, card);
			CHECK(withCard.hasCard());
			render(withCard, 4.0);
			pressKey(withCard, ProEButton::Style);
			pressKey(withCard, ProEButton::Bank5);
			pressKey(withCard, ProEButton::Number1);
			pressKey(withCard, ProEButton::StartStop);
			for(const uint8_t note : {0x30, 0x34, 0x37})
				withCard.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x90, note, 0x64));
			const auto cardPeak = render(withCard, 3.0);
			std::printf("card: LCD |%s|%s|, peak %d\n", lcdLine(withCard, 0).c_str(), lcdLine(withCard, 1).c_str(),
				cardPeak);
			CHECK(lcdLine(withCard, 0).rfind("C1 ", 0) == 0);
			CHECK(cardPeak > 1000);
			CHECK(withCard.machine().cpu().illegal_instructions() == 0);
		}
	}

	return test::finish("proe_test");
}
