#include "88lib/mcu/sc88_submcu.h"
#include "cpu/common/test_util.hpp"

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
		f.input({0xf0, 0x41, 0x10, 0x42, 0x11, 0x0c, 0, 0, 0, 0, 0, 0x74, 0xf7}, 1);
		CHECK(f.records.empty()); // RQ1 must never be applied as parameter data
		f.input({0xf0, 0x43, 0x10, 0x4c, 0, 0, 0x7e, 0, 0xf7});
		CHECK_EQ(f.records.size(), 1u);
		if(f.records.empty()) return;
		CHECK_EQ(f.records[0].command, 0xec);
		CHECK_EQ(f.records[0].param1, 7);
		CHECK_EQ(f.records[0].payload[0], 0x43);
	}
}

int main()
{
	outputRing();
	delayAndReset();
	rawRequests();
	return finish("sc88_submcu");
}
