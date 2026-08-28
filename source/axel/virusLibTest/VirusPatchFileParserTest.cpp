#include "virusJucePlugin/VirusPatchFileParser.h"

#include "virusLib/microcontrollerTypes.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
	using Bytes = synthLib::SysexBuffer;

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
		result[8] = _program;
		result.back() = 0xf7;
		return result;
	}

	Bytes makeTIControlState(const size_t _singleCount)
	{
		std::vector<Bytes> messages;
		messages.push_back(makeSysex(267, virusLib::DUMP_MULTI, 0));
		for(uint8_t part = 0; part < _singleCount; ++part)
			messages.push_back(makeSysex(524, virusLib::DUMP_SINGLE, part));

		Bytes body;
		appendU32(body, 0);
		for(const auto& message : messages)
		{
			appendU32(body, message.size());
			body.insert(body.end(), message.begin(), message.end());
		}

		Bytes state{'M', 'I', 'D', 'I'};
		appendU32(state, body.size());
		state.insert(state.end(), body.begin(), body.end());
		return state;
	}

	bool testTIControlArrangementIsMerged()
	{
		const auto state = makeTIControlState(16);
		synthLib::SysexBufferList results;

		bool result = true;
		result &= expect(genericVirusUI::VirusPatchFileParser::parse(results, state, {}),
			"TI Control state is accepted by the preset parser");
		result &= expect(results.size() == 1, "multi and 16 singles are merged into one arrangement");
		if(results.size() != 1)
			return false;

		constexpr size_t multiSize = 267;
		constexpr size_t singleSize = 524;
		result &= expect(results[0].size() == multiSize + 16 * singleSize,
			"arrangement retains the complete multi and single dumps");
		result &= expect(results[0][6] == virusLib::DUMP_MULTI, "arrangement begins with the multi dump");
		for(size_t part = 0; part < 16; ++part)
		{
			const auto offset = multiSize + part * singleSize;
			result &= expect(results[0][offset + 6] == virusLib::DUMP_SINGLE,
				"each arrangement part remains a single dump");
			result &= expect(results[0][offset + 8] == part, "arrangement retains each part number");
		}
		return result;
	}

	bool testIncompleteArrangementIsNotMerged()
	{
		const auto state = makeTIControlState(15);
		synthLib::SysexBufferList results;

		return expect(genericVirusUI::VirusPatchFileParser::parse(results, state, {}),
			"incomplete TI Control state still exposes its valid dumps")
			&& expect(results.size() == 16, "multi plus 15 singles are not misclassified as an arrangement");
	}

	bool testRawSysexFallbackStillLoads()
	{
		const auto single = makeSysex(524, virusLib::DUMP_SINGLE, 7);
		synthLib::SysexBufferList results;

		return expect(genericVirusUI::VirusPatchFileParser::parse(results, single, {}),
			"ordinary raw SysEx still loads")
			&& expect(results.size() == 1 && results.front() == single,
				"raw SysEx fallback preserves the original dump");
	}
}

bool testVirusPatchFileParser()
{
	return testTIControlArrangementIsMerged()
		&& testIncompleteArrangementIsNotMerged()
		&& testRawSysexFallbackStillLoads();
}
