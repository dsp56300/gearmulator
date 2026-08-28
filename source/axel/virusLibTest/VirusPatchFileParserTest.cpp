#include "FixtureLoader.h"

#include "virusJucePlugin/VirusPatchFileParser.h"

#include "virusLib/microcontrollerTypes.h"

#include <cstdint>
#include <iostream>
#include <string>
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
		const auto state = virusLibTest::loadHexFixture("3Slimey-processor-state.hex");
		synthLib::SysexBufferList results;

		bool result = true;
		result &= expect(genericVirusUI::VirusPatchFileParser::parse(results, state, {}),
			"captured TI Control ProcessorState is accepted by the preset parser");
		result &= expect(results.size() == 3,
			"captured multi and 16 singles merge while two parameter messages remain separate");
		if(results.size() != 3)
			return false;

		constexpr size_t multiSize = 267;
		constexpr size_t singleSize = 524;
		result &= expect(results[0].size() == multiSize + 16 * singleSize,
			"captured arrangement retains the complete multi and single dumps");
		result &= expect(results[0][6] == virusLib::DUMP_MULTI, "arrangement begins with the multi dump");
		result &= expect(std::string(reinterpret_cast<const char*>(results[0].data() + multiSize + 249), 10) == "3Slimey   ",
			"captured arrangement retains the first part name");
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
		const auto single = virusLibTest::loadHexFixture("3Slimey_part0.syx.hex");
		synthLib::SysexBufferList results;

		return expect(genericVirusUI::VirusPatchFileParser::parse(results, single, {}),
			"captured single-patch SysEx loads")
			&& expect(results.size() == 1 && results.front() == single,
				"single-patch import preserves the captured dump")
			&& expect(std::string(reinterpret_cast<const char*>(results.front().data() + 249), 10) == "3Slimey   ",
				"single-patch import retains its name");
	}

	bool testCapturedSingleExportsRoundTrip()
	{
		const auto expected = virusLibTest::loadBinaryFixture("single.syx");
		const auto midi = virusLibTest::loadBinaryFixture("single.mid");
		synthLib::SysexBufferList syxResults;
		synthLib::SysexBufferList midiResults;

		bool result = true;
		result &= expect(genericVirusUI::VirusPatchFileParser::parse(syxResults, expected, "single.syx"),
			"captured Single SysEx export parses");
		result &= expect(syxResults.size() == 1 && syxResults.front() == expected,
			"captured Single SysEx export remains byte-identical");
		result &= expect(genericVirusUI::VirusPatchFileParser::parse(midiResults, midi, "single.mid"),
			"captured Single MIDI export parses");
		result &= expect(midiResults.size() == 1 && midiResults.front() == expected,
			"captured Single MIDI export contains the same SysEx dump");
		return result;
	}

	bool testCapturedArrangementExportsRoundTrip()
	{
		const auto expected = virusLibTest::loadBinaryFixture("arrangement.syx");
		const auto midi = virusLibTest::loadBinaryFixture("arrangement.mid");
		synthLib::SysexBufferList syxResults;
		synthLib::SysexBufferList midiResults;

		bool result = true;
		result &= expect(expected.size() == 267 + 16 * 524,
			"captured Arrangement SysEx contains one Multi and 16 Singles");
		result &= expect(genericVirusUI::VirusPatchFileParser::parse(syxResults, expected, "arrangement.syx"),
			"captured Arrangement SysEx export parses");
		result &= expect(syxResults.size() == 1 && syxResults.front() == expected,
			"captured Arrangement SysEx export merges without changing bytes");
		result &= expect(genericVirusUI::VirusPatchFileParser::parse(midiResults, midi, "arrangement.mid"),
			"captured Arrangement MIDI export parses");
		result &= expect(midiResults.size() == 1 && midiResults.front() == expected,
			"captured Arrangement MIDI export contains the same Multi and 16 Singles");
		return result;
	}

	bool testCapturedPartialBankLoadsAsPatches()
	{
		const auto bank = virusLibTest::loadBinaryFixture("Bank.mid");
		synthLib::SysexBufferList results;
		constexpr uint8_t programs[] = {0, 1, 2, 3, 4, 5, 6, 21, 56, 127};
		constexpr const char* names[] = {
			"3Slimey   ", "3Slimey!  ", "3Slimey!! ", "35limey!! ", "SSLLIIMMEE",
			"EEEEEEEE  ", "Rave!!!   ", "5Slimey   ", "5Slimier  ", "5Slimist  "
		};

		bool result = true;
		result &= expect(genericVirusUI::VirusPatchFileParser::parse(results, bank, "Bank.mid"),
			"captured partial MIDI bank loads");
		result &= expect(results.size() == 10, "partial bank exposes exactly its 10 stored patches without padding");
		if(results.size() != 10)
			return false;

		for(size_t index = 0; index < results.size(); ++index)
		{
			const auto& patch = results[index];
			result &= expect(patch.size() == 524, "every partial bank entry is a complete single dump");
			if(patch.size() != 524)
				continue;
			result &= expect(patch[6] == virusLib::DUMP_SINGLE, "partial bank entries remain separate single patches");
			result &= expect(patch[7] == 1, "partial bank entries retain Bank A");
			result &= expect(patch[8] == programs[index], "partial bank retains sparse program numbers and ordering");
			result &= expect(std::string(reinterpret_cast<const char*>(patch.data() + 249), 10) == names[index],
				"partial bank retains the patch name at each stored program");
		}
		return result;
	}

	bool testCapturedBankLoadsAsPatches()
	{
		const auto bank = virusLibTest::loadBinaryFixture("BankFull.mid");
		synthLib::SysexBufferList results;

		bool result = true;
		result &= expect(genericVirusUI::VirusPatchFileParser::parse(results, bank, "BankFull.mid"),
			"captured 128-patch bank loads");
		result &= expect(results.size() == 128, "bank exposes all 128 patches to the data source");
		if(results.size() != 128)
			return false;

		for(size_t program = 0; program < results.size(); ++program)
		{
			const auto& patch = results[program];
			result &= expect(patch.size() == 524, "every bank entry is a complete single dump");
			if(patch.size() != 524)
				continue;
			result &= expect(patch[6] == virusLib::DUMP_SINGLE, "every bank entry is a single patch");
			result &= expect(patch[7] == 1, "every bank entry retains Bank A");
			result &= expect(patch[8] == program, "every bank entry retains its program number");
		}

		result &= expect(std::string(reinterpret_cast<const char*>(results.front().data() + 249), 10) == "3Slimey   ",
			"bank retains its first patch name");
		result &= expect(std::string(reinterpret_cast<const char*>(results.back().data() + 249), 10) == "3Slimey   ",
			"bank retains its last patch name");
		constexpr size_t boundaryPrograms[] = {16, 32, 48, 64, 80, 96, 112, 126};
		constexpr const char* boundaryNames[] = {
			"SSSSSS    ", "IIIIIIIII ", "MMMMMM    ", "EEEEEEEE  ",
			"YYYYYYY   ", "5Slimist  ", "TRAPGOD   ", "Signed,   "
		};
		for(size_t index = 0; index < sizeof(boundaryPrograms) / sizeof(boundaryPrograms[0]); ++index)
		{
			const auto& patch = results[boundaryPrograms[index]];
			result &= expect(patch.size() == 524
				&& std::string(reinterpret_cast<const char*>(patch.data() + 249), 10) == boundaryNames[index],
				"full bank retains patch names at program-group boundaries and the penultimate slot");
		}
		return result;
	}
}

bool testVirusPatchFileParser()
{
	return testTIControlArrangementIsMerged()
		&& testIncompleteArrangementIsNotMerged()
		&& testRawSysexFallbackStillLoads()
		&& testCapturedSingleExportsRoundTrip()
		&& testCapturedArrangementExportsRoundTrip()
		&& testCapturedPartialBankLoadsAsPatches()
		&& testCapturedBankLoadsAsPatches();
}
