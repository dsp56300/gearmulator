#pragma once

#include "88lib/mt32/mt32.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The parameters of each kind of entry of the MT-32 dialect's memory, with the ranges the firmware takes. A value
// above its maximum is stored as the maximum on every board, never ignored; the names and the few bytes the
// manuals call dummies are stored as they come or not at all, see storeByte().
namespace emu88Lib::mt32
{
	// The kinds of entry, each with its own parameter list
	enum class Block : uint8_t
	{
		LaSystem,
		PatchTemp,		// parts 1-8 and the rhythm part, which uses only some of it
		RhythmKey,
		PatchMemory,
		TimbreCommon,	// offsets 00-0D of a timbre
		Partial,		// one of the four partials of a timbre, offsets relative to the partial
		PcmSystem,
		PcmPatchTemp,
		PcmPatchMemory,
		Count
	};

	// How a value reads
	enum class ParamText : uint8_t
	{
		Number,
		Signed,			// value - center
		OffOn,
		Name,			// ASCII
		TimbreGroup,	// a, b, i (memory), r (rhythm)
		TimbreNumber,	// 1-64
		AssignMode,		// POLY 1-4: single or multi assign, priority to the last or the first note
		ReverbMode,		// Room, Hall, Plate, Tap Delay
		MasterTune,		// Hz
		PanLa,			// 0-14, R7 .. L7: the LA board's pan runs from the right
		PanPcm,			// 0-127, right to left too
		MidiChannel,	// 1-16, OFF
		RhythmTimbre,	// i01-i64 (memory), r01-r64
		Structure,		// 1-13
		PartialMute,	// a bit per partial, set = playing
		EnvMode,		// Normal, No Sustain
		PitchCoarse,	// C1 - C9
		PitchKeyfollow,
		TvfKeyfollow,
		Waveform,		// SQU/1, SAW/1, SQU/2, SAW/2: the bank of a PCM partial in bit 1
		PcmWave,		// 1-128
		BiasPoint,		// <A1 - <C7, >A1 - >C7
		BiasLevelTva,	// -12 - 0
		ToneMedia,		// Internal, Card
		ToneNumber,		// 1-128
		Note,			// C-1 - G9
	};

	struct Param
	{
		const char* id;
		const char* section;
		const char* label;
		uint8_t offset;
		uint8_t max;		// what the board takes; an LA parameter's maximum on the new firmware
		uint8_t maxOld;		// on the old LA firmware, where it differs; else max
		ParamText text;
		uint8_t center = 0;	// for Signed
		uint8_t size = 1;	// the timbre name is ten bytes
	};

	const std::vector<Param>& params(Block _block);
	Block blockOf(Area _area);
	// The size of an entry of the block
	size_t blockSize(Block _block);
	// The parameter at an offset of a block, -1 for none (a dummy byte)
	int findParam(Block _block, uint8_t _offset);
	int findParamById(Block _block, const std::string& _id);

	uint8_t maxValue(const Param& _param, LaFirmware _firmware);

	// What a board stores for a byte written to an offset of a block: false when it keeps what it has, else the
	// value it stores in _value. The LA board's partial reserves have rules of their own on top, see Memory.
	bool storeByte(Block _block, uint8_t _offset, LaFirmware _firmware, uint8_t& _value);

	// The entries of a value list, from 0; empty for a parameter shown as a number
	std::vector<std::string> valueList(const Param& _param, LaFirmware _firmware = LaFirmware::New);
	std::string formatValue(const Param& _param, uint8_t _value);
	std::string noteName(uint8_t _note);

	// A timbre's structures, 0-12 as the board keeps them, shown 1-13: what each makes of a pair of partials, 1 & 2
	// or 3 & 4. A partial plays a synth wave or a PCM sample, and the two are mixed, ring modulated with the first
	// added, ring modulated alone, or sent left and right. The plain mix has no synth first partial with a PCM second,
	// and a stereo pair is of one kind.
	enum class StructureMix : uint8_t { Mix, RingAndFirst, Ring, Stereo };
	struct Structure
	{
		bool pcm[2];
		StructureMix mix;
	};
	Structure structure(uint8_t _value);
	// The value for a mix of two partials of these kinds; false when there is none
	bool structureValue(StructureMix _mix, bool _firstPcm, bool _secondPcm, uint8_t& _value);
	// "(P1 + S2 -> R) + P1": S a synth partial, P a PCM one, R the ring modulator. _firstPartial is 1 or 3.
	std::string structureName(uint8_t _value, int _firstPartial);

	// A PCM partial's sample by its PCM Wave, 0-127, in its waveform's bank: bank 0 the MT-32's, bank 1 the CM-32L's
	// own, which the MT-32 1.x does not have. The names are MT32edit4v's.
	const char* pcmWaveName(uint8_t _bank, uint8_t _wave);
}
