#include "virusLib/import/TIControlStateDecoder.h"
#include "virusLib/microcontrollerTypes.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Bytes = synthLib::SysexBuffer;
	using Events = std::vector<synthLib::SMidiEvent>;

	bool expect(const bool _condition, const char* const _message)
	{
		if(_condition)
			return true;

		std::cerr << "FAILED: " << _message << '\n';
		return false;
	}

	void appendU32(Bytes& _data, const size_t _value)
	{
		_data.push_back(static_cast<uint8_t>(_value >> 24));
		_data.push_back(static_cast<uint8_t>(_value >> 16));
		_data.push_back(static_cast<uint8_t>(_value >> 8));
		_data.push_back(static_cast<uint8_t>(_value));
	}

	Bytes makeSysex(const size_t _size, const uint8_t _command, const uint8_t _program)
	{
		Bytes result(_size, 0);
		result[0] = 0xf0;
		result[1] = 0x00;
		result[2] = 0x20;
		result[3] = 0x33;
		result[4] = 0x01;
		result[5] = 0x10;
		result[6] = _command;
		result[7] = 0x00;
		result[8] = _program;
		result.back() = 0xf7;
		return result;
	}

	Bytes makeMidiBlock(const std::vector<Bytes>& _messages)
	{
		Bytes body;
		appendU32(body, 0); // Controller-assignment data is absent in the captured Live state.

		for(const auto& message : _messages)
		{
			appendU32(body, message.size());
			body.insert(body.end(), message.begin(), message.end());
		}

		Bytes result{'M', 'I', 'D', 'I'};
		appendU32(result, body.size());
		result.insert(result.end(), body.begin(), body.end());
		return result;
	}

	std::vector<Bytes> makeCapturedStateMessages()
	{
		std::vector<Bytes> messages;
		messages.push_back(makeSysex(267, virusLib::DUMP_MULTI, 0));

		constexpr const char* names[] =
		{
			"HOOV44    ", "PLAY WITH ", "TBLIPS    ", "HOVBASS   ",
			"HOVSINE   ", "BsMoovin'@", "HOVSINE   ", "HOOV44 VAR",
			"HOOV44    ", "HOOV44    ", "Auto 5ths@", " -Init-   ",
			"HOOV44 VAR", "ArpBass2JL", " -Init-   ", " -Init-   "
		};

		for(uint8_t part = 0; part < 16; ++part)
		{
			auto single = makeSysex(524, virusLib::DUMP_SINGLE, part);
			for(size_t i = 0; i < 10; ++i)
				single[249 + i] = static_cast<uint8_t>(names[part][i]);
			messages.push_back(std::move(single));
		}

		messages.push_back(makeSysex(11, virusLib::PAGE_D, 2));
		messages.push_back(makeSysex(11, virusLib::PAGE_D, 2));

		constexpr uint8_t controllers[] = {1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16};
		for(const auto controller : controllers)
		{
			for(uint8_t channel = 0; channel < 16; ++channel)
				messages.push_back(Bytes{static_cast<uint8_t>(synthLib::M_CONTROLCHANGE + channel), controller, channel});
		}

		return messages;
	}

	bool testCapturedStateShape()
	{
		const auto state = makeMidiBlock(makeCapturedStateMessages());
		Events events;

		bool result = true;
		result &= expect(virusLib::TIControlStateDecoder::decode(events, state), "captured-shape state parses");
		result &= expect(events.size() == 243, "all 243 MIDI records are returned");

		if(events.size() != 243)
			return false;

		result &= expect(events[0].sysex.size() == 267 && events[0].sysex[6] == virusLib::DUMP_MULTI,
			"the multi dump is first");

		for(size_t part = 0; part < 16; ++part)
		{
			const auto& event = events[part + 1];
			result &= expect(event.sysex.size() == 524 && event.sysex[6] == virusLib::DUMP_SINGLE,
				"each part contains a single dump");
			result &= expect(event.sysex[8] == part, "single dumps retain their part numbers");
		}
		result &= expect(std::string(reinterpret_cast<const char*>(events[1].sysex.data() + 249), 10) == "HOOV44    ",
			"single dump payload and patch name are retained");

		size_t sysexCount = 0;
		size_t midiCount = 0;
		for(const auto& event : events)
		{
			if(event.sysex.empty())
				++midiCount;
			else
				++sysexCount;
		}

		result &= expect(sysexCount == 19, "all 19 SysEx records are returned");
		result &= expect(midiCount == 224, "all 224 controller records are returned");
		result &= expect(events[19].a == synthLib::M_CONTROLCHANGE && events[19].b == 1 && events[19].c == 0,
			"controller records retain their bytes");
		return result;
	}

	bool testEmbeddedAndMultipleBlocks()
	{
		Bytes state{'p', 'r', 'e', 'f', 'i', 'x'};
		const auto first = makeMidiBlock({makeSysex(11, virusLib::PAGE_D, 1)});
		const auto second = makeMidiBlock({makeSysex(11, virusLib::PAGE_D, 2)});
		state.insert(state.end(), first.begin(), first.end());
		state.insert(state.end(), second.begin(), second.end());

		Events events;
		bool result = true;
		result &= expect(virusLib::TIControlStateDecoder::decode(events, state), "embedded MIDI blocks parse");
		result &= expect(events.size() == 2, "multiple MIDI blocks are parsed");
		if(events.size() == 2)
		{
			result &= expect(events[0].sysex[8] == 1, "first block retains its data");
			result &= expect(events[1].sysex[8] == 2, "second block retains its data");
		}
		return result;
	}

	bool testMalformedLengthIsRejected()
	{
		Bytes state{'M', 'I', 'D', 'I'};
		appendU32(state, 1000);
		appendU32(state, 0);

		Events events;
		return expect(!virusLib::TIControlStateDecoder::decode(events, state), "oversized block is rejected")
			&& expect(events.empty(), "malformed block produces no events");
	}
}

int main()
{
	bool result = true;
	result &= testCapturedStateShape();
	result &= testEmbeddedAndMultipleBlocks();
	result &= testMalformedLengthIsRejected();

	if(result)
		std::cout << "TIControlStateDecoder tests passed\n";

	return result ? 0 : 1;
}
