#include "88lib/mcu/sc8850_submcu.h"
#include "cpu/common/test_util.hpp"

using namespace emu88Lib;

namespace
{
	constexpr uint32_t Tx = 0x00540000;
	constexpr uint32_t Rx = 0x00580000;

	void checkByte(Sc8850SubMcu& mcu, uint8_t status, uint8_t value)
	{
		CHECK_EQ(mcu.hostRead(Rx + 1), status);
		CHECK_EQ(mcu.hostRead(Rx + 1), status);
		CHECK_EQ(mcu.hostRead(Rx), value);
	}

	void packet(Sc8850SubMcu& mcu, uint8_t header, uint8_t a, uint8_t b = 0, uint8_t c = 0)
	{
		mcu.hostWrite(Tx + 1, header);
		for(auto value : {a, b, c}) mcu.hostWrite(Tx, value);
	}

	void outputRouting(Sc8850SubMcu& mcu, Sc8850SubMcu::BootProtocol protocol)
	{
		const size_t cables = protocol == Sc8850SubMcu::BootProtocol::Sc8820 ? 2 : 4;
		for(uint8_t cable = 0; cable < 4; ++cable)
			packet(mcu, uint8_t((cable << 4) | 9), 0x90, uint8_t(60 + cable), 100);
		packet(mcu, 0xf9, 0x90, 80, 100); // Unknown cable.
		packet(mcu, 0x01, 0x90, 81, 100); // Reserved CIN.
		std::vector<synthLib::SMidiEvent> events;
		mcu.readMidiOut(events);
		CHECK_EQ(events.size(), cables);
		for(size_t i = 0; i < events.size(); ++i)
		{
			CHECK_EQ(events[i].port, i);
			CHECK_EQ(events[i].a, 0x90);
			CHECK_EQ(events[i].b, 60 + i);
			CHECK_EQ(events[i].c, 100);
			CHECK(events[i].source == synthLib::MidiEventSource::Device);
		}
		events.clear();
		packet(mcu, 0x04, 0xf0, 0x41, 1);
		packet(mcu, 0x14, 0xf0, 0x42, 2);
		packet(mcu, 0x15, 0xf7);
		packet(mcu, 0x0f, 0xf8);
		packet(mcu, 0x06, 3, 0xf7);
		mcu.readMidiOut(events);
		CHECK_EQ(events.size(), 3u);
		if(events.size() == 3)
		{
			CHECK_EQ(events[0].port, 1);
			CHECK(events[0].sysex == synthLib::SysexBuffer({0xf0, 0x42, 2, 0xf7}));
			CHECK_EQ(events[1].port, 0);
			CHECK_EQ(events[1].a, 0xf8);
			CHECK_EQ(events[2].port, 0);
			CHECK(events[2].sysex == synthLib::SysexBuffer({0xf0, 0x41, 1, 3, 0xf7}));
		}
		events.clear();
		mcu.readMidiOut(events);
		CHECK(events.empty());
		packet(mcu, 0x19, 0x90, 60, 100);
		mcu.reset();
		mcu.readMidiOut(events);
		CHECK(events.empty());
	}

	void exercise(Sc8850SubMcu::BootProtocol protocol)
	{
		Sc8850SubMcu mcu(protocol);
		checkByte(mcu, 0xe1, 0);
		checkByte(mcu, 0xf1, 0);
		checkByte(mcu, 1, 0xfb);
		mcu.hostWrite(Tx + 1, 0x12);
		checkByte(mcu, 1, 0xfc);
		mcu.hostWrite(Tx + 1, 0x34);
		checkByte(mcu, 1, 0xfe);
		std::vector<uint8_t> program(Sc8850SubMcu::ProgramSize);
		for(size_t i = 0; i < program.size(); ++i)
		{
			program[i] = uint8_t(i ^ (i >> 8));
			mcu.hostWrite(Tx + 1, program[i]);
		}
		CHECK(mcu.uploadedProgram() == program);
		if(protocol == Sc8850SubMcu::BootProtocol::Sc8820)
			checkByte(mcu, 0x91, 0);
		else
		{
			checkByte(mcu, 1, 0xf0);
			mcu.hostWrite(Tx, 0);
		}
		CHECK(!mcu.bootComplete());
		checkByte(mcu, 1, 0xff);
		CHECK(mcu.bootComplete());
		CHECK(!mcu.ready());
		const uint8_t noteA[]{0x90, 60, 100}, noteB[]{0x91, 67, 90};
		mcu.midiIn(0, noteA, sizeof(noteA));
		mcu.midiIn(1, noteB, sizeof(noteB));
		for(uint32_t i = 1; i < Sc8850SubMcu::EnumerationDelayTicks; ++i) mcu.tick();
		CHECK(!mcu.ready());
		mcu.tick();
		CHECK(mcu.ready());
		checkByte(mcu, 5, 0);
		checkByte(mcu, 0x55, 0x09);
		for(auto byte : noteA) checkByte(mcu, 0x51, byte);
		checkByte(mcu, 0x55, 0x19);
		for(auto byte : noteB) checkByte(mcu, 0x51, byte);
		CHECK_EQ(mcu.inputBacklog(), 0u);
		mcu.midiIn(2, noteA, sizeof(noteA));
		CHECK_EQ(mcu.inputBacklog(), protocol == Sc8850SubMcu::BootProtocol::Sc8820 ? 0u : 4u);
		outputRouting(mcu, protocol);
		CHECK(!mcu.bootComplete());
		CHECK(!mcu.ready());
		CHECK(mcu.uploadedProgram().empty());
		checkByte(mcu, 0xe1, 0);
	}
}

int main()
{
	exercise(Sc8850SubMcu::BootProtocol::Sc8850);
	exercise(Sc8850SubMcu::BootProtocol::Sc8820);
	return test::finish("sc8850_submcu");
}
