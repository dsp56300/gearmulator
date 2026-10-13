#pragma once

#include "88lib/deviceModel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The GS parameters the part editor shows besides the tone: the global ones (system, reverb, chorus, delay, equalizer)
// and those of a part (mixer, pitch, tone modify, setup, scale tuning). Which board has which, their values after a GS
// Reset, the ranges the boards take and the channel messages that set them too were read back from the emulated
// firmware with data requests.
namespace emu88Lib::gs
{
	// The Sound Canvas generations, by the parameters they have
	enum class Generation : uint8_t
	{
		Sc55,		// the SC-55 family: reverb and chorus
		Sc88,		// the SC-88, SC-88VL, XP-GS: delay, equalizer, scale tuning, part EQ
		Sc88Pro,	// the SC-88Pro, VE-GS Pro: the insertion effect
		Sc8850,		// the SC-8850, SC-8820
	};
	Generation generation(DeviceModel _model);

	// The first SC-55 programs: the SC-55, SC-155, CM-300 and SCC-1A
	bool isFirstSc55Program(DeviceModel _model);

	// How a value reads
	enum class ParamText : uint8_t
	{
		Number,
		Signed,			// value - 64
		Pan,			// L63 .. C .. R63, 64 the centre
		PartPan,		// the same with 0 random
		OffOn,
		MonoPoly,
		PartMode,		// normal, drum map 1, drum map 2
		RxChannel,		// 1-16, off
		Note,
		ReverbMacro,
		ChorusMacro,
		DelayMacro,
		EqLowFreq,
		EqHighFreq,
		Decibel,		// value - 64 dB
		Cents,			// value - 64 cents
		MasterTune,		// 24-2024, 1024 the centre, 0.1 cents a step
		DelayRatio,		// 4 % a step
		Milliseconds,
		Semitones,
		FineTune,		// 100 cents over 64 steps
		EfxType,		// the insertion effect type, MSB << 8 | LSB
		EfxParam,		// what the insertion effect type makes of it
		EfxSource,		// off, a controller, channel aftertouch, bend
	};

	// The part parameters the tracker keeps in fields of its own, with logic of their own
	enum class PartField : uint8_t { None, Level, Pan, RxChannel, UseForRhythm };

	// What a board makes of a value out of its range
	enum class OutOfRange : uint8_t
	{
		Clamp,			// takes the nearest end; a switch takes anything but 0 as on
		Ignore,
		IgnoreOn8850,	// the SC-8850 ignores it, the others clamp it
		RawOn8850,		// the SC-8850 takes any value, the others ignore it
	};

	struct Param
	{
		const char* id;
		const char* section;
		const char* label;
		// The SysEx address, a part's with the block nibble 0: 40 1x yy as 0x401000 | yy. 0 for a parameter that only
		// a channel message sets.
		uint32_t address;
		uint16_t min, max, def;
		ParamText text;
		Generation since;
		// The bytes at the address, which a data set has to bring all of: four nibbles for the master tune, MSB and
		// LSB for the fine tune
		uint8_t size = 1;
		// The SysEx address is younger than the parameter: the SC-55s set the fine tune by RPN only
		Generation sysexSince = Generation::Sc55;
		// A controller that sets it too
		uint8_t cc = 0xff;
		// The RPN (MSB 0) that sets it
		uint8_t rpn = 0xff;
		// The NRPN that sets it
		uint8_t nrpnMsb = 0xff;
		uint8_t nrpnLsb = 0xff;
		PartField field = PartField::None;
		OutOfRange outOfRange = OutOfRange::Clamp;
	};

	constexpr size_t g_maxPartParams = 48;
	constexpr size_t g_maxGlobalParams = 72;

	const std::vector<Param>& globalParams();
	const std::vector<Param>& partParams();

	bool isAvailable(const Param& _param, DeviceModel _model);
	// Whether the board has its SysEx address
	bool hasAddress(const Param& _param, DeviceModel _model);
	// What the board takes; the SC-55s take less for the tone modify
	std::pair<uint16_t, uint16_t> valueRange(const Param& _param, DeviceModel _model);
	// A value written to the board, as the board takes it: false when it ignores it
	bool acceptValue(const Param& _param, DeviceModel _model, uint16_t& _value);

	// A part parameter's address for a part of a group, 0-15 = part 1-16
	uint32_t partParamAddress(uint8_t _part, const Param& _param);

	// By address; a part parameter by its address with the block nibble taken out. -1 for none.
	int findGlobalParam(uint32_t _address);
	int findPartParam(uint32_t _address);
	int findGlobalParamById(const std::string& _id);
	int findPartParamById(const std::string& _id);
	int findPartParamByController(uint8_t _cc);
	int findPartParamByRpn(uint8_t _lsb);
	int findPartParamByNrpn(uint8_t _msb, uint8_t _lsb);

	// The entries of a value list, from the parameter's min on; empty for a parameter shown as a number
	std::vector<std::string> valueList(const Param& _param);
	// The same with the value of each entry: the insertion effect types are not numbered from the min
	std::vector<std::pair<uint16_t, std::string>> valueChoices(const Param& _param);
	std::string formatValue(const Param& _param, uint16_t _value);

	// What selecting a reverb (40 01 30), chorus (40 01 38) or delay (40 01 50) macro sets the effect's other
	// parameters to, as (address, value) pairs; empty for any other address or a macro out of range
	std::vector<std::pair<uint32_t, uint8_t>> macroPreset(uint32_t _macroAddress, uint8_t _value);

	constexpr uint32_t g_masterTune = 0x400000;

	// The insertion effect parameter, 0-19, a global parameter's address is for; -1 for any other
	int efxParamIndex(uint32_t _address);
}
