#include "88lib/gs/toneCatalog.h"

#include <algorithm>

// The SC-55 family's tone tables, the same in every program ROM from the SC-55 1.21 to the SC-55mkII and SC-55st.
// The board maps CPU pages 1-4 onto ROM 0x10000, 0x20000, 0x30000 and 0x00000 (Sc55Board::romAddress()), and the
// program change handler (SC-55 1.21 04:0922, mkII 04:2CF2) reads:
//   tone map     03:0000  u16 big endian per [bank][program], FFFF = nothing there
//   tones        01:0000 + n * 0xD8 for n < 0xE0, 02:0000 + (n - 0xE0) * 0xD8 above (tone address routine,
//                1.21 04:0575), 216 bytes each, the 12 character name first
//   drum set map 03:8000  u8 set number per program, FF = nothing there
//   drum sets    03:8080 + n * 0x48C, 1164 bytes each, the 12 character name last
// Bank select LSB is never read: one map.
namespace emu88Lib::gs
{
	namespace
	{
		constexpr size_t g_toneMap = 0x30000;
		constexpr size_t g_toneSize = 0xd8;
		constexpr size_t g_drumSetMap = 0x38000;
		constexpr size_t g_drumSets = 0x38080;
		constexpr size_t g_drumSetSize = 0x48c;
		constexpr size_t g_drumSetName = 0x480;
		constexpr size_t g_nameLength = 12;

		bool readName(const std::vector<uint8_t>& _rom, const size_t _offset, std::string& _name)
		{
			if(_offset + g_nameLength > _rom.size())
				return false;
			_name.clear();
			for(size_t i = 0; i < g_nameLength; ++i)
			{
				const auto c = _rom[_offset + i];
				if(c < 0x20 || c > 0x7e)
					return false;
				_name.push_back(static_cast<char>(c));
			}
			while(!_name.empty() && _name.back() == ' ')
				_name.pop_back();
			return true;
		}

		// The program change on a drum part checks the drum set map, and a set number it has nothing at falls back
		// below a limit only: CMP:E #H'40,R0 in the SC-55 1.21 (04:096E), #H'30 in the SC-155 and the CM-300 1.20.
		// The compare follows the map lookup, CMP.B #H'FF,@(-H'8000,R0) with DP on page 3, and its BNE.
		uint8_t findDrumLimit(const std::vector<uint8_t>& _rom)
		{
			constexpr uint8_t lookup[] = {0xf0, 0x80, 0x00, 0x04, 0xff, 0x26};
			for(auto it = _rom.begin(); ; ++it)
			{
				it = std::search(it, _rom.end(), std::begin(lookup), std::end(lookup));
				if(std::distance(it, _rom.end()) < static_cast<ptrdiff_t>(std::size(lookup) + 4))
					return 0x40;
				// BNE d8, then CMP:E #imm,R0 and BCS
				const auto* cmp = &*it + std::size(lookup) + 1;
				if(cmp[0] == 0x40 && cmp[2] == 0x25)
					return cmp[1];
			}
		}
	}

	bool extractSc55Catalog(const std::vector<uint8_t>& _program, const DeviceModel _model, ToneCatalog& _catalog,
		std::string& _error)
	{
		if(_program.size() < g_drumSets)
		{
			_error = "The program ROM is too small.";
			return false;
		}

		ToneMap map;
		map.number = 0;
		map.name = getDeviceProfile(_model).displayName;

		for(uint32_t bank = 0; bank < 128; ++bank)
		{
			for(uint32_t program = 0; program < 128; ++program)
			{
				const auto entry = g_toneMap + (bank * 128 + program) * 2;
				const auto n = static_cast<uint32_t>(_program[entry] << 8 | _program[entry + 1]);
				if(n == 0xffff)
					continue;
				const auto offset = n < 0xe0 ? 0x10000 + n * g_toneSize : 0x20000 + (n - 0xe0) * g_toneSize;
				Tone tone;
				tone.bankMsb = static_cast<uint8_t>(bank);
				tone.program = static_cast<uint8_t>(program);
				if(offset >= g_toneMap || !readName(_program, offset, tone.name))
				{
					_error = "The program ROM's tone map is not one the editor knows.";
					return false;
				}
				map.tones.push_back(std::move(tone));
			}
		}

		for(uint32_t program = 0; program < 128; ++program)
		{
			const auto n = _program[g_drumSetMap + program];
			if(n == 0xff)
				continue;
			DrumSet set;
			set.program = static_cast<uint8_t>(program);
			if(!readName(_program, g_drumSets + n * g_drumSetSize + g_drumSetName, set.name))
			{
				_error = "The program ROM's drum set map is not one the editor knows.";
				return false;
			}
			map.drumSets.push_back(std::move(set));
		}

		// Every program of bank 0 is a GM tone, and STANDARD is on program 1 of every SC-55
		if(map.tones.size() < 128 || map.drumSets.empty() || map.drumSets.front().program != 0)
		{
			_error = "The program ROM's tone tables are not where the editor expects them.";
			return false;
		}

		std::sort(map.tones.begin(), map.tones.end(), [](const Tone& _a, const Tone& _b)
		{
			return std::pair{_a.program, _a.bankMsb} < std::pair{_b.program, _b.bankMsb};
		});

		_catalog.maps.clear();
		_catalog.maps.push_back(std::move(map));

		// The program change handlers differ in what they do with a number the maps have nothing at, found by
		// running each handler on every bank and program. The first SC-55 programs substitute (1.21 04:09B9); the
		// SCC-1A and the SCB-55 keep that code but never reach it and fall silent; the mkII programs say
		// "No INSTRUMENT" and fall silent (04:2E35), or "No DRUM SET" and keep the set they had, and the SC-55st
		// keeps the tone as well once 40 1x 25 is set (04:2E67). Before the mkII, a drum part ignores a program
		// change sent with bank select other than 0.
		switch(_model)
		{
		case DeviceModel::Sc55Mk1:
		case DeviceModel::Sc155:
		case DeviceModel::Cm300:
			_catalog.missingTone = MissingTone::SubCapital;
			_catalog.missingDrumSet = MissingDrumSet::SubCapital;
			_catalog.drumLimit = findDrumLimit(_program);
			_catalog.drumBankMustBeZero = true;
			break;
		case DeviceModel::Scc1a:
		case DeviceModel::Scb55:
			_catalog.missingTone = MissingTone::Silent;
			_catalog.missingDrumSet = MissingDrumSet::Silent;
			_catalog.drumBankMustBeZero = true;
			break;
		case DeviceModel::Sc55St:
			_catalog.missingTone = MissingTone::KeepIfFlagged;
			_catalog.missingDrumSet = MissingDrumSet::Ignored;
			break;
		default:
			// The SC-55mkII and the SC-155mkII. The RLP-3237's program is not on disk to check; it is taken to
			// behave like the mkII it is built on.
			_catalog.missingTone = MissingTone::Silent;
			_catalog.missingDrumSet = MissingDrumSet::Ignored;
			break;
		}
		return true;
	}
}
