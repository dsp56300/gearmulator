#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The insertion effect of the SC-88Pro, VE-GS Pro, SC-8850 and SC-8820: its type at 40 03 00 (MSB, LSB), twenty
// parameters at 40 03 03-16 whose meaning the type gives, and the sends and controls after them.
namespace emu88Lib::gs
{
	// How a parameter's value reads. Value shows the number the board receives.
	enum class EfxFormat : uint8_t
	{
		Value,
		Gain,
		Pan,
		Feedback,
		Semitones,
		Cents,
		LowFreq,
		HighFreq,
		Q,
		AmpType,
		FilterType,
		Polarity,
		ModWave,
		Output,
		Vowel,
		ReverbType,
		GateType,
		RotarySpeed,
		FeedbackMode,
		Phase,
		PreFilter,
		OdSelect,
		ChorusFlanger,
		TremoloPan,
		PostGain,
		Ratio,
		MonoStereo,
		OffOn,
	};

	struct EfxParam
	{
		uint8_t index;		// 0-19, at 40 03 (03 + index)
		const char* name;
		uint8_t min;
		uint8_t max;
		EfxFormat format;
		// The SC-88Pro ignores a value out of the range; one it does not check it takes as it comes. The SC-8850
		// checks none.
		bool checked;
	};

	constexpr size_t g_efxParamCount = 20;

	struct EfxType
	{
		uint16_t id;		// MSB << 8 | LSB
		const char* name;
		// What selecting the type sets the twenty parameters to
		uint8_t defaults[g_efxParamCount];
		uint16_t firstParam;
		uint8_t paramCount;
	};

	constexpr uint32_t g_efxTypeAddress = 0x400300;
	constexpr uint32_t g_efxParamAddress = 0x400303;
	// What a type selection sets the sends to reverb, chorus and delay (40 03 17-19) to as well
	constexpr uint8_t g_efxSendDefaults[3] = {40, 0, 0};

	const EfxType* efxTypes();
	size_t efxTypeCount();
	// The type of an id; Thru for one the boards do not know, which they take as Thru too
	const EfxType& findEfxType(uint16_t _id);
	bool isEfxType(uint16_t _id);
	// The parameter a type has at an index, nullptr where it has none
	const EfxParam* findEfxParam(const EfxType& _type, size_t _index);
	std::vector<const EfxParam*> efxParams(const EfxType& _type);

	// The entries of a parameter shown as a choice, from its min on; empty for one shown as a number
	std::vector<std::string> efxValueList(const EfxParam& _param);
	std::string formatEfxValue(const EfxParam& _param, uint8_t _value);
	// 40 03 1B and 1D: off, a controller, channel aftertouch, bend
	std::string formatEfxSource(uint8_t _value);
}
