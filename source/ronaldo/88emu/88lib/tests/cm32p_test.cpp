#include "88lib/boards/cm32p.h"
#include "common/test_util.hpp"

#include <algorithm>
#include "common/romDescramble.h"

namespace
{
	struct Program
	{
		std::vector<uint8_t> bytes = std::vector<uint8_t>(emu88Lib::Cm32p::ProgramRomSize, 0xff);
		size_t cursor = 0x2080;
		void emit(std::initializer_list<uint8_t> code)
		{
			std::copy(code.begin(), code.end(), bytes.begin() + cursor);
			cursor += code.size();
		}
		Program() { emit({0xb1, 0, 0x0e, 0xb1, 0x80, 0x0e}); } // UART divisor 1.
		void write(uint16_t address, uint8_t value)
		{
			emit({0xb1, value, 0x20, 0xc7, 1, uint8_t(address), uint8_t(address >> 8), 0x20});
		}
		void transmit(uint16_t address)
		{
			emit({0xb3, 1, uint8_t(address), uint8_t(address >> 8), 7});
			emit({0xb1, 40, 0x22, 0xe0, 0x22, 0xfd}); // Wait for the serial frame.
		}
		void finish() { emit({0x27, 0xfe}); CHECK(cursor <= 0x2100); }
	};

	// Scrambles decoded card data into a raw image with the given address line order, the inverse
	// of the board's decoding.
	std::vector<uint8_t> scrambleCard(const std::vector<uint8_t>& decoded, const std::array<unsigned, 19>& lines)
	{
		constexpr std::array<unsigned, 8> dataLines{1,2,7,3,5,0,4,6};
		std::vector<uint8_t> raw(decoded.size());
		for(uint32_t source = 0; source < raw.size(); ++source)
		{
			uint32_t address = 0;
			for(const auto bit : lines) address = (address << 1) | ((source >> bit) & 1);
			uint8_t value = 0;
			for(unsigned i = 0; i < 8; ++i)
				value = static_cast<uint8_t>(value | (((decoded[address] >> (7 - i)) & 1) << dataLines[i]));
			raw[source] = value;
		}
		return raw;
	}

	std::vector<synthLib::SMidiEvent> run(emu88Lib::Cm32p& board)
	{
		for(unsigned i = 0; i < 200; ++i)
		{
			const auto audio = board.renderSample();
			(void)audio;
		}
		std::vector<synthLib::SMidiEvent> events;
		board.readMidiOut(events);
		return events;
	}
}

int main()
{
	std::array<uint8_t, 3> partial{1, 2, 3}, untouched{0x55, 0x55, 0x55};
	bool rejected = false;
	try { rLib::rom::Pcm8::scramble(partial.data(), partial.size(), untouched.data()); }
	catch(const std::invalid_argument&) { rejected = true; }
	CHECK(rejected);
	CHECK((untouched == std::array<uint8_t, 3>{0x55, 0x55, 0x55}));
	std::vector<uint8_t> logical(rLib::rom::Pcm8::BlockSize), raw(logical.size()), decoded(logical.size());
	for(size_t i = 0; i < logical.size(); ++i) logical[i] = static_cast<uint8_t>(i ^ (i >> 8));
	rLib::rom::Pcm8::scramble(logical.data(), logical.size(), raw.data());
	rLib::rom::Pcm8::descramble(raw.data(), raw.size(), decoded.data(), decoded.size());
	CHECK(logical == decoded);
	using emu88Lib::Cm32p;
	Cm32p invalid(emu88Lib::Cm32pRomSet{});
	CHECK(!invalid.isValid());
	CHECK(invalid.renderSample() == Cm32p::SampleFrame{});
	Cm32p::WaveRoms waves;
	for(auto& wave : waves) wave.resize(Cm32p::WaveRomSize);
	// Physical address bit 5 becomes decoded bit 1; these bytes decode to FE/F8/FA.
	waves[0][32] = 0xbf;
	waves[1][32] = 0xae;
	waves[2][32] = 0xbe;
	Program pcm;
	for(const auto bank : {0x00, 0x10, 0x20, 0x08})
	{
		pcm.write(0x1403, static_cast<uint8_t>(bank));
		pcm.transmit(0x1401);
	}
	pcm.finish();
	Cm32p board({pcm.bytes, waves});
	CHECK(board.isValid());
	for(unsigned cycle = 0; cycle < 2; ++cycle)
	{
		const auto events = run(board);
		CHECK_EQ(events.size(), 4u);
		if(events.size() == 4)
		{
			CHECK_EQ(events[0].a, 0xfe);
			CHECK_EQ(events[1].a, 0xf8);
			CHECK_EQ(events[2].a, 0xfa);
			CHECK_EQ(events[3].a, 0xff); // Empty PCM-card aperture.
		}
		board.reset();
	}
	Program rcc;
	rcc.write(0x1081, 0xfe);
	rcc.write(0x1082, 0xf8);
	rcc.write(0x1086, 0x42);
	rcc.write(0x108a, 0x42);
	rcc.transmit(0x1081);
	rcc.transmit(0x1082);
	rcc.finish();
	Cm32p effects({rcc.bytes, waves});
	const auto events = run(effects);
	CHECK_EQ(events.size(), 2u);
	if(events.size() == 2)
	{
		CHECK_EQ(events[0].a, 0xfe);
		CHECK_EQ(events[1].a, 0xf8);
	}
	Program lcd;
	lcd.write(0x1100, 0x38); // Two-line, 8-bit interface.
	lcd.write(0x1100, 0x0c); // Display on.
	lcd.write(0x1100, 0x80);
	lcd.write(0x1102, 'A');
	lcd.write(0x1100, 0xc0);
	lcd.write(0x1102, 'B');
	lcd.finish();
	Cm32p display({lcd.bytes, waves});
	run(display);
	CHECK_EQ(display.lcd().getVisibleColumns(), 16u);
	CHECK_EQ(display.lcd().getVisibleLines(), 2u);
	CHECK(display.lcd().isDisplayOn());
	CHECK_EQ(display.lcd().getVisibleCharacter(0, 0), 'A');
	CHECK_EQ(display.lcd().getVisibleCharacter(1, 0), 'B');
	display.reset();
	CHECK(!display.lcd().isDisplayOn());
	CHECK_EQ(display.lcd().getVisibleCharacter(0, 0), ' ');

	// Like the firmware queue, do not send another character until LCD INT.
	// This stalls after the cursor command when the board omits the handshake.
	Program lcdQueue;
	lcdQueue.write(0x1100, 0x38);
	lcdQueue.write(0x1100, 0x0c);
	lcdQueue.emit({0xb1, 0, 9}); // Clear initialization interrupts.
	lcdQueue.write(0x1100, 0x80);
	for(const auto character : {'C', 'M'})
	{
		lcdQueue.emit({0xb0, 9, 0x24, 0x34, 0x24, 0xfa}); // Wait for HSI0 pending.
		lcdQueue.emit({0xb1, 0, 9});
		lcdQueue.write(0x1102, character);
	}
	lcdQueue.finish();
	Cm32p queuedDisplay({lcdQueue.bytes, waves});
	for(unsigned boot = 0; boot < 2; ++boot)
	{
		run(queuedDisplay);
		CHECK_EQ(queuedDisplay.lcd().getVisibleCharacter(0, 0), 'C');
		CHECK_EQ(queuedDisplay.lcd().getVisibleCharacter(0, 1), 'M');
		queuedDisplay.reset();
	}

	Program echo;
	// Poll RX-ready, echo SBUF, and loop. The UART runs faster than the paced input.
	echo.emit({0xb0, 0x11, 0x20, 0x36, 0x20, 0xfa, 0xb0, 7, 7, 0x27, 0xf5});
	Cm32p midi({echo.bytes, waves});
	synthLib::SMidiEvent note(synthLib::MidiEventSource::Host, 0x90, 60, 100);
	midi.addMidiEvent(note, 1);
	CHECK(run(midi).empty());
	midi.addMidiEvent(note);
	const auto echoed = run(midi);
	CHECK_EQ(echoed.size(), 1u);
	if(echoed.size() == 1)
	{
		CHECK_EQ(echoed[0].a, 0x90);
		CHECK_EQ(echoed[0].b, 60);
		CHECK_EQ(echoed[0].c, 100);
		CHECK(echoed[0].source == synthLib::MidiEventSource::Device);
	}
	note.cancelOnTransportChange = true;
	midi.addMidiEvent(note);
	midi.transportDiscontinuity(1);
	const auto cancelled = run(midi);
	CHECK_EQ(cancelled.size(), 1u);
	if(cancelled.size() == 1)
	{
		CHECK_EQ(cancelled[0].a, 0xb0);
		CHECK_EQ(cancelled[0].b, 120);
		CHECK_EQ(cancelled[0].c, 0);
	}
	midi.addMidiEvent(note);
	midi.reset();
	CHECK(run(midi).empty());

	// Serializer slot 2 is the left DAC channel (the firmware's own "PCM OUT L" test lands
	// there). Park a value in RCC RAM A[7], use it as output 2's bias and enable the serializer.
	Program routing;
	const std::pair<uint16_t, uint8_t> routingWrites[]{
		{0x1080, 0x10}, {0x1081, 0x00}, {0x1082, 0x00}, {0x1084, 7},	// RAM A[7] = 0x100000
		{0x1080, 0x00}, {0x1081, 0x0e}, {0x1082, 0x00}, {0x1086, 92},	// output 2 bias: RAM A[7]
		{0x108d, 0x40}};
	for(const auto& [address, value] : routingWrites)
		routing.write(address, value);
	routing.finish();
	Cm32p routed({routing.bytes, waves});
	Cm32p::SampleFrame frame{};
	// The board fades in: C89 starts discharged, so the VCA needs a few of its 8.2 ms time
	// constants before the DAC word reaches the output unattenuated.
	for(unsigned i = 0; i < 8 * 262; ++i)
		frame = routed.renderSample();
	CHECK(std::abs(frame.first - (0x100000 >> 8) * 256) < (0x100000 >> 8) * 256 / 200);
	CHECK_EQ(frame.second, 0);

	// A synthetic PCM card: eight tone names where the tone list starts, and a byte at the LP
	// readback's address. Scrambled in either dump order, it decodes to the same window.
	std::vector<uint8_t> cardData(Cm32p::CardSize, 0xff);
	for(uint32_t tone = 0; tone < 8; ++tone)
		std::copy_n("TEST TONE ", 10, cardData.begin() + 0x1000 + tone * 0x50);
	cardData[2] = 0xfb;
	constexpr std::array<unsigned, 19> boardLines{18,17,15,14,16,12,11,7,9,13,10,8,3,2,1,6,4,5,0};
	constexpr std::array<unsigned, 19> altLines{18,17,8,9,16,11,12,7,14,10,13,15,3,2,1,6,4,5,0};
	const auto boardOrder = scrambleCard(cardData, boardLines);
	CHECK(Cm32p::decodeCard(boardOrder) == cardData);
	CHECK(Cm32p::decodeCard(scrambleCard(cardData, altLines)) == cardData);
	CHECK(Cm32p::decodeCard({}).empty());
	CHECK(Cm32p::decodeCard(std::vector<uint8_t>(Cm32p::CardSize + 1, 0xff)).empty());
	CHECK(Cm32p::decodeCard(std::vector<uint8_t>(Cm32p::CardSize, 0xff)).empty()); // No tone list.
	// A 128 KiB card repeats through the window.
	const auto mirrored = Cm32p::decodeCard({boardOrder.begin(), boardOrder.begin() + 0x20000});
	CHECK_EQ(mirrored.size(), size_t{Cm32p::CardSize});
	for(uint32_t offset = 0; offset < Cm32p::CardSize && mirrored.size() == Cm32p::CardSize; offset += 0x20000)
		CHECK(std::equal(cardData.begin(), cardData.begin() + 0x20000, mirrored.begin() + offset));

	// The LP reaches the card through its window, and the firmware sees it on P0.4.
	Program slot;
	slot.write(0x1403, 0x08);
	slot.transmit(0x1401);
	slot.emit({0xb1, 0xfc, 0x30, 0x34, 0x0e, 0x03, 0xb1, 0xfa, 0x30, 0xb0, 0x30, 7}); // FA if P0.4 is high, else FC.
	slot.emit({0xb1, 40, 0x22, 0xe0, 0x22, 0xfd}); // Wait for the serial frame.
	slot.finish();
	for(const bool inserted : {false, true})
	{
		Cm32p slotBoard({slot.bytes, waves}, inserted ? boardOrder : std::vector<uint8_t>{});
		CHECK_EQ(slotBoard.hasCard(), inserted);
		const auto events = run(slotBoard);
		CHECK_EQ(events.size(), 2u);
		if(events.size() == 2)
		{
			CHECK_EQ(events[0].a, inserted ? 0xfb : 0xff);
			CHECK_EQ(events[1].a, inserted ? 0xfa : 0xfc);
		}
	}

	waves[2].pop_back();
	Cm32p incomplete({pcm.bytes, waves});
	CHECK(!incomplete.isValid());
	return test::finish("cm32p board");
}
