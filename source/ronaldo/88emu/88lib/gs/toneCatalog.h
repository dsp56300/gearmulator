#pragma once

#include "88lib/deviceModel.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emu88Lib::gs
{
	struct Tone
	{
		uint8_t bankMsb = 0;
		uint8_t program = 0;
		std::string name;
	};

	struct DrumSet
	{
		uint8_t program = 0;
		std::string name;
	};

	// The tones and drum sets of one tone map, as the board's ROM lists them.
	struct ToneMap
	{
		// What bank select LSB (CC#32) selects it with; 0 on a board with one map
		uint8_t number = 0;
		std::string name;
		// By program, then bank
		std::vector<Tone> tones;
		// By program
		std::vector<DrumSet> drumSets;
		// Banks 64 and 65 hold the user tones, programs 64 and 65 the user drum sets. They live in the board's
		// backed up RAM, not in its ROM, so only the board knows their names.
		bool userTones = false;

		const Tone* find(uint8_t _bankMsb, uint8_t _program) const;
		const DrumSet* findDrumSet(uint8_t _program) const;
		// The tones of a program, by bank
		std::vector<const Tone*> variations(uint8_t _program) const;
	};

	// What a firmware does with a program change its map has no tone for
	enum class MissingTone : uint8_t
	{
		Silent,				// the part plays nothing; the SC-55mkII says "No INSTRUMENT"
		SubCapital,			// the first SC-55 programs: below bank 64 and program 120, bank & F8, then bank 0
		KeepIfFlagged,		// the part's 40 1x 25 decides: set, the program change is ignored, clear, silent
		KeepIfFlaggedBelow,	// the VE-GS Pro: the same, but ignored only below bank 64 and program 120
	};

	// And with one for a drum set
	enum class MissingDrumSet : uint8_t
	{
		Silent,
		Ignored,		// the program change is ignored and the part keeps its set; the boards say "No DRUM SET"
		SubCapital,		// the first SC-55 programs: below the drum limit, program & F8, then STANDARD
	};

	// What a program change ends up doing on a part
	enum class Selection : uint8_t
	{
		Tone,		// a tone of the map, or one the firmware substitutes
		User,		// a user tone or drum set
		Silent,		// nothing: the part plays no notes
		Ignored,	// the part keeps what it had
	};

	// What the board can play, read from its own ROMs: every tone map with its tones and drum sets, and what the
	// firmware does with a program change that selects nothing.
	class ToneCatalog
	{
	public:
		std::vector<ToneMap> maps;
		MissingTone missingTone = MissingTone::Silent;
		MissingDrumSet missingDrumSet = MissingDrumSet::Silent;
		// The first drum set number MissingDrumSet::SubCapital no longer substitutes for
		uint8_t drumLimit = 0x40;
		// The first SC-55 programs ignore a program change on a drum part when bank select is not 0
		bool drumBankMustBeZero = false;
		// An ignored program change puts the map latch back too; the SC-8850 keeps the bank select LSB it brought
		// for the next one
		bool ignoreRestoresMap = true;
		// Maps a part can be set to that the catalog leaves out, like the SC-88Pro's XG one. Any other map number
		// selects nothing.
		std::vector<uint8_t> hiddenMaps;

		bool empty() const { return maps.empty(); }

		// The map with that number, null when the catalog has none: a map the editor does not list, like the
		// SC-88Pro's hidden XG one
		const ToneMap* findMap(uint8_t _number) const;

		struct ToneResult
		{
			Selection selection = Selection::Silent;
			const Tone* tone = nullptr;
		};

		struct DrumSetResult
		{
			Selection selection = Selection::Silent;
			const DrumSet* set = nullptr;
		};

		// What a program change for a tone number does on a normal part, _keep the part's 40 1x 25
		ToneResult selectTone(uint8_t _map, uint8_t _bankMsb, uint8_t _program, bool _keep) const;
		// The same on a drum part
		DrumSetResult selectDrumSet(uint8_t _map, uint8_t _program) const;
	};

	// Reads the board's ROMs through the ROM loader and extracts its tone lists. Null with _error set when the
	// images are missing or not ones the extractor knows.
	std::shared_ptr<const ToneCatalog> loadToneCatalog(DeviceModel _model, std::string& _error);

	// The SC-55 family keeps its tables in the program ROM. _program as the ROM loader reads it.
	bool extractSc55Catalog(const std::vector<uint8_t>& _program, DeviceModel _model, ToneCatalog& _catalog,
		std::string& _error);

	// The SC-88 family, the SC-88Pro and the boards running their programs keep theirs in the control ROM.
	bool extractSc88Catalog(const std::vector<uint8_t>& _control, DeviceModel _model, ToneCatalog& _catalog,
		std::string& _error);

	// The SC-8850 and the SC-8820 keep theirs in their flash and mask ROMs, with the lookup code in the program. The
	// images at the addresses the boards map them to.
	struct RomRegion
	{
		uint32_t base = 0;
		std::vector<uint8_t> data;
		bool code = false;
	};
	bool extractSc8850Catalog(const std::vector<RomRegion>& _regions, DeviceModel _model, ToneCatalog& _catalog,
		std::string& _error);
}
