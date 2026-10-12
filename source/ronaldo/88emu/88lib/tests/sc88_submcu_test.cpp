#include "88lib/mcu/sc88_submcu.h"
#include "cpu/common/test_util.hpp"
#include "baseLib/os.h"

using namespace emu88Lib;
using namespace test;

namespace
{
	struct Fixture
	{
		std::vector<Sc88SubMcu::Record> records;
		Sc88SubMcu mcu{[this](auto&& r) { records.emplace_back(std::move(r)); }, 0x14, 0x6c};
		Sc88SubMcu::SharedRam ram{};
		Fixture() { mcu.startOutput(ram); }
		void run(unsigned samples) { while(samples--) mcu.clockOutput(ram, 32000); }
		void input(std::initializer_list<uint8_t> bytes, uint8_t source = 0)
		{
			for(auto byte : bytes) mcu.midiIn(source, byte);
		}
		uint8_t publish(const std::vector<uint8_t>& bytes)
		{
			const auto start = ram[Sc88SubMcu::TxWrite];
			auto pos = start;
			ram[pos] = static_cast<uint8_t>(bytes.size());
			for(auto byte : bytes)
			{
				if(++pos == 0xc0) pos = 0x24;
				ram[pos] = byte;
			}
			const auto end = static_cast<uint8_t>(0x24 + (start - 0x24 + ((bytes.size() + 4) & ~3u)) % 0x9c);
			mcu.commitOutput(ram, end);
			ram[Sc88SubMcu::TxWrite] = end;
			return end;
		}
	};

	void outputRing()
	{
		Fixture f;
		// Start near the end to exercise a length-prefixed packet crossing 0xc0.
		f.ram[Sc88SubMcu::TxRead] = f.ram[Sc88SubMcu::TxWrite] = 0xbc;
		const std::vector<uint8_t> message{0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0, 0x7f, 0, 0x41, 0xf7};
		const auto end = f.publish(message);
		CHECK_EQ(end, 0x2c);
		// Receive staging must not change bytes already committed for output.
		std::fill(f.ram.begin(), f.ram.begin() + 0xc0, 0);
		f.run(112); // eleven bytes take 112.64 samples at DIN MIDI baud
		std::vector<synthLib::SMidiEvent> out;
		f.mcu.readMidiOut(out);
		CHECK(out.empty());
		CHECK(f.records.empty());
		f.run(1);
		f.mcu.readMidiOut(out);
		CHECK_EQ(out.size(), 1u);
		if(!out.empty()) CHECK(std::equal(message.begin(), message.end(), out[0].sysex.begin(), out[0].sysex.end()));
		CHECK_EQ(f.ram[Sc88SubMcu::TxRead], end);
		CHECK_EQ(f.records.size(), 1u);
		if(!f.records.empty()) CHECK_EQ(f.records[0].command, 0xe1);
	}

	void delayAndReset()
	{
		Fixture f;
		f.publish({0xff, 0, 45});
		f.publish({0x90, 60, 100});
		f.run(1441);
		std::vector<synthLib::SMidiEvent> out;
		f.mcu.readMidiOut(out);
		CHECK(out.empty()); // internal FF command is never MIDI System Reset
		f.run(31);
		f.mcu.readMidiOut(out);
		CHECK_EQ(out.size(), 1u);
		if(!out.empty()) CHECK_EQ(out[0].a, 0x90);
		f.publish({0x90, 61, 100});
		f.mcu.reset();
		f.mcu.startOutput(f.ram);
		f.run(1000);
		out.clear();
		f.mcu.readMidiOut(out);
		CHECK(out.empty());
		CHECK_EQ(f.ram[Sc88SubMcu::TxRead], f.ram[Sc88SubMcu::TxWrite]);
	}

	void rawRequests()
	{
		Fixture f;
		f.input({0xf0, 0x43, 0x10, 0x4c, 0, 0, 0x7e, 0, 0xf7});
		CHECK_EQ(f.records.size(), 1u);
		if(f.records.empty()) return;
		CHECK_EQ(f.records[0].command, 0xec);
		CHECK_EQ(f.records[0].param1, 7);
		CHECK_EQ(f.records[0].payload[0], 0x43);
	}

	// RQ1 goes to the CPU as a request: bit 7 of the staged length, so it is never applied as
	// parameter data. The opcode and the channel nibble name the address the way the firmware's
	// transmitter reads them back, and the staged bytes keep the 41 <dev> 42 11 header.
	void requests()
	{
		Fixture f;
		f.input({0xf0, 0x41, 0x10, 0x42, 0x11, 0x40, 0x11, 0x19, 0, 0, 1, 0x15, 0xf7}, 1);
		CHECK_EQ(f.records.size(), 1u);
		if(f.records.empty()) return;
		const auto& level = f.records[0];
		CHECK_EQ(level.command, 0x61);
		CHECK_EQ(level.channel, 0x91); // Bulk, from IN B, middle address nibble 1.
		CHECK_EQ(level.param1, 0x8b);
		CHECK_EQ(level.param2, 0x14);
		CHECK(level.payload == std::vector<uint8_t>({0x41, 0x10, 0x42, 0x11, 0x40, 0x11, 0x19, 0, 0, 1, 0x15}));

		// Module B keeps its own opcode: the CPU picks the module, not the sub-MCU.
		f.records.clear();
		f.input({0xf0, 0x41, 0x10, 0x42, 0x11, 0x50, 0x00, 0x04, 0, 0, 1, 0x2b, 0xf7});
		f.input({0xf0, 0x41, 0x10, 0x42, 0x11, 0x41, 0x02, 0x3c, 0, 0, 1, 0x00, 0xf7});
		f.input({0xf0, 0x41, 0x10, 0x42, 0x11, 0x0c, 0, 0, 0, 0, 0, 0x74, 0xf7}, 1);
		CHECK_EQ(f.records.size(), 3u);
		if(f.records.size() < 3) return;
		CHECK_EQ(f.records[0].command, 0x70);
		CHECK_EQ(f.records[0].channel, 0x80);
		CHECK_EQ(f.records[1].command, 0x80);
		CHECK_EQ(f.records[1].channel, 0x82);
		CHECK_EQ(f.records[2].command, 0x30);
		CHECK_EQ(f.records[2].channel, 0x90);

		// A bad checksum is reported like a DT1's; a short request and an unknown block are dropped.
		f.records.clear();
		f.input({0xf0, 0x41, 0x10, 0x42, 0x11, 0x40, 0x11, 0x19, 0, 0, 1, 0x16, 0xf7});
		CHECK_EQ(f.records.size(), 1u);
		if(!f.records.empty()) CHECK_EQ(f.records[0].command, 0xe0);
		f.records.clear();
		f.input({0xf0, 0x41, 0x10, 0x42, 0x11, 0x40, 0x11, 0x19, 1, 0x15, 0xf7});
		f.input({0xf0, 0x41, 0x10, 0x42, 0x11, 0x30, 0, 0, 0, 0, 1, 0x4f, 0xf7});
		CHECK(f.records.empty());
	}

	void universalMessages()
	{
		// GM System On and Master Volume reach the CPU staged, like XG: its handlers read the header
		// themselves, and the Pro's check the exact length. A bare record is queued unread.
		Fixture f;
		f.input({0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7}, 1);
		f.input({0xf0, 0x7f, 0x7f, 0x04, 0x01, 0x00, 0x64, 0xf7});
		f.input({0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7}); // Identity Request has no handler.
		CHECK_EQ(f.records.size(), 2u);
		if(f.records.size() < 2) return;
		const auto& gm = f.records[0];
		CHECK_EQ(gm.command, 0xee);
		CHECK_EQ(gm.channel, 0x90); // Bulk, from IN B.
		CHECK_EQ(gm.param1, 4);
		CHECK_EQ(gm.param2, 0x14);
		CHECK(gm.payload == std::vector<uint8_t>({0x7e, 0x7f, 0x09, 0x01}));
		const auto& volume = f.records[1];
		CHECK_EQ(volume.command, 0xef);
		CHECK_EQ(volume.channel, 0x80);
		CHECK_EQ(volume.param1, 6);
		CHECK(volume.payload == std::vector<uint8_t>({0x7f, 0x7f, 0x04, 0x01, 0x00, 0x64}));
	}

	void channelMessageRxPort()
	{
		for(uint8_t part = 0; part < 32; ++part)
		{
			Fixture f;
			const auto checksum = static_cast<uint8_t>((-1 - part) & 0x7f);
			f.input({0xf0, 0x41, 0x10, 0x42, 0x12, 0, 1, part, 0, checksum, 0xf7}, 1);
			CHECK_EQ(f.records.size(), 1u);
			if(f.records.empty()) continue;
			const auto& r = f.records.front();
			CHECK_EQ(r.command, 0x20);
			CHECK_EQ(r.channel, 0x90); // Original physical source, bulk flag.
			CHECK(r.payload == std::vector<uint8_t>({0, 0, 0, 0, 0, 1, part, 0, checksum}));
			// Routing state belongs to firmware: channel events stay on their input.
			f.input({0x90, 60, 100});
			CHECK_EQ(f.records.size(), 2u);
			CHECK_EQ(f.records.back().channel, 0);
		}
		Fixture f;
		f.input({0xf0, 0x41, 0x10, 0x42, 0x12, 0, 1, 0x10, 0, 0x70, 0xf7});
		CHECK_EQ(f.records.size(), 1u);
		CHECK_EQ(f.records.front().command, 0xe0); // Bad checksum cannot change routing.
		f.records.clear();
		f.input({0xf0, 0x41, 0x10, 0x42, 0x12, 0, 0, 0x7f, 1, 0, 0xf7});
		CHECK_EQ(f.records.size(), 1u);
		CHECK_EQ(f.records.front().command, 0x20); // System Mode Set uses same family.
	}

	// The blocks below 0x10 travel in the 0x20 family too: the CPU drops any other opcode below
	// 0x20 but its display records. 0C 20 04 is the Pro's SC-88 mode, the one SELECT+ALL toggles.
	void lowBlocks()
	{
		for(const uint8_t block : {uint8_t{0x08}, uint8_t{0x0c}})
		{
			Fixture f;
			const auto checksum = static_cast<uint8_t>((0x80 - ((block + 0x20 + 0x04 + 0x01) & 0x7f)) & 0x7f);
			f.input({0xf0, 0x41, 0x10, 0x42, 0x12, block, 0x20, 0x04, 0x01, checksum, 0xf7});
			CHECK_EQ(f.records.size(), 1u);
			if(f.records.empty()) continue;
			const auto& r = f.records.front();
			CHECK_EQ(r.command, 0x20);
			CHECK_EQ(r.channel, 0x80);
			CHECK(r.payload == std::vector<uint8_t>({0, 0, 0, 0, block, 0x20, 0x04, 0x01, checksum}));
		}
		// The part block keeps its own opcode.
		Fixture f;
		f.input({0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x41, 0x19, 0x00, 0x66, 0xf7});
		CHECK_EQ(f.records.size(), 1u);
		if(!f.records.empty()) CHECK_EQ(f.records.front().command, 0x40);
	}
}

int main()
{
	baseLib::disableErrorDialogs();

	outputRing();
	delayAndReset();
	rawRequests();
	requests();
	universalMessages();
	channelMessageRxPort();
	lowBlocks();
	return finish("sc88_submcu");
}
