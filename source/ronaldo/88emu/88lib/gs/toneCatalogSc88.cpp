#include "88lib/gs/toneCatalog.h"

#include <algorithm>
#include <iterator>

// The SC-88 generation's tone tables, all in the control ROM; the wave ROMs hold samples only. A CPU address on the
// table pages 02-0C is the offset into the image as the ROM loader reads it.
//
// Both generations describe their pointer tables at 02:FFA0: the last valid tone row, then page and offset of the
// tone pointer table (FFA1, FFA2) and of the drum set pointer table (FFA5, FFA6). The tone pointer table holds a
// u24 per (row, program), low word FFFF for none; a tone record starts with its 12 character name, a drum set
// record has it at +0x500. Bank select LSB is the map number, 0 meaning the part's map-0 one (40 4x 01).
//
// SC-88, SC-88VL, XP-GS: two maps. 02:FC00 is the tone row per (map - 1, bank), 02:FD00 the drum set per (map - 1,
// program), 80 and up for none (program change 1.01 00:2D3E).
//
// SC-88Pro, SC-GS A '96, VE-GS Pro: four descriptors at 02:FFF0, each FF, page, u16 offset: the tone slot per map,
// the tone row per (slot, bank), the drum slot per map and the drum set per (slot, program), FF for none (1.02
// 00:3A1F). The page byte of a pointer carries flags in bits 5-7: 0x40 is never selectable, 0x20 marks what an
// SC-88 does not have, which the SC-88 mode switch 0C 20 04 turns off, 0x80 a tone that plays two (Violin).
namespace emu88Lib::gs
{
	namespace
	{
		struct Image
		{
			const std::vector<uint8_t>& rom;

			uint8_t u8(const uint32_t _a) const { return _a < rom.size() ? rom[_a] : 0xff; }
			uint16_t u16(const uint32_t _a) const { return static_cast<uint16_t>(u8(_a) << 8 | u8(_a + 1)); }
			uint32_t u24(const uint32_t _a) const { return static_cast<uint32_t>(u8(_a)) << 16 | u16(_a + 1); }

			bool name(const uint32_t _a, std::string& _name) const
			{
				if(_a + 12 > rom.size())
					return false;
				_name.clear();
				for(uint32_t i = 0; i < 12; ++i)
				{
					// The firmware masks the name bytes for the display
					const auto c = static_cast<char>(u8(_a + i) & 0x7f);
					if(c < 0x20 || c > 0x7e)
						return false;
					_name.push_back(c);
				}
				while(!_name.empty() && _name.back() == ' ')
					_name.pop_back();
				return true;
			}
		};

		struct MapDesc
		{
			uint8_t number;
			const char* name;
			bool userTones;
		};

		// The maps a part can be set to; the SC-88Pro's 117-119 (the two halves of its dual tones and an XG map)
		// are hidden and left out. The user banks are on every map but the SC-55 one.
		constexpr MapDesc g_sc88Maps[] = {{1, "SC-55", false}, {2, "SC-88", true}};
		constexpr MapDesc g_proMaps[] = {{1, "SC-55", false}, {2, "SC-88", true}, {3, "SC-88Pro", true}};
	}

	bool extractSc88Catalog(const std::vector<uint8_t>& _control, const DeviceModel _model, ToneCatalog& _catalog,
		std::string& _error)
	{
		const Image img{_control};
		const bool pro = _model == DeviceModel::Sc88Pro || _model == DeviceModel::VeGsPro || _model == DeviceModel::Sk88Pro;

		if(_control.size() < 0x30000 || (pro && (img.u8(0x2fff0) != 0xff || img.u8(0x2fff1) == 0xff)))
		{
			_error = "The control ROM's tone tables are not where the editor expects them.";
			return false;
		}

		const auto maxRow = img.u8(0x2ffa0);
		const auto toneTable = static_cast<uint32_t>(img.u8(0x2ffa1)) << 16 | img.u16(0x2ffa2);
		const auto drumTable = static_cast<uint32_t>(img.u8(0x2ffa5)) << 16 | img.u16(0x2ffa6);
		uint32_t desc[4] = {};
		for(uint32_t i = 0; i < 4 && pro; ++i)
			desc[i] = static_cast<uint32_t>(img.u8(0x2fff1 + 4 * i)) << 16 | img.u16(0x2fff2 + 4 * i);

		// The record a pointer table entry points at, 0 for none
		const auto record = [&](const uint32_t _entry, const bool _isTone) -> uint32_t
		{
			if((_entry & 0xffff) == 0xffff)
				return 0;
			if(!pro)
				return _entry;
			const auto flags = (_entry >> 16) & 0xe0;
			if(_isTone && (flags & 0x40))
				return 0;
			return ((_entry >> 16) & 0x1f) << 16 | (_entry & 0xffff);
		};

		const auto* mapDescs = pro ? g_proMaps : g_sc88Maps;
		const auto mapCount = pro ? std::size(g_proMaps) : std::size(g_sc88Maps);

		_catalog.maps.clear();
		for(size_t m = 0; m < mapCount; ++m)
		{
			const auto& mapDesc = mapDescs[m];
			ToneMap map;
			map.number = mapDesc.number;
			map.name = mapDesc.name;
			map.userTones = mapDesc.userTones;

			const uint32_t toneSlot = pro ? img.u8(desc[0] + mapDesc.number) : mapDesc.number - 1u;
			const uint32_t drumSlot = pro ? img.u8(desc[2] + mapDesc.number) : mapDesc.number - 1u;
			if(toneSlot >= 0x80 || drumSlot >= 0x80)
			{
				_error = "The control ROM has no " + map.name + " map.";
				return false;
			}

			for(uint32_t bank = 0; bank < 128; ++bank)
			{
				const auto row = pro ? img.u8(desc[1] + toneSlot * 0x80 + bank) : img.u8(0x2fc00 + toneSlot * 0x80 + bank);
				if((pro ? row == 0xff : (row & 0x80) != 0) || row > maxRow)
					continue;
				for(uint32_t program = 0; program < 128; ++program)
				{
					const auto rec = record(img.u24(toneTable + row * 0x180u + program * 3u), true);
					if(!rec)
						continue;
					Tone tone;
					tone.bankMsb = static_cast<uint8_t>(bank);
					tone.program = static_cast<uint8_t>(program);
					if(!img.name(rec, tone.name))
					{
						_error = "The control ROM's tone table is not one the editor knows.";
						return false;
					}
					map.tones.push_back(std::move(tone));
				}
			}

			for(uint32_t program = 0; program < 128; ++program)
			{
				const auto index = img.u8((pro ? desc[3] : 0x2fd00u) + drumSlot * 0x80 + program);
				if(index & 0x80)
					continue;
				const auto rec = record(img.u24(drumTable + index * 3u), false);
				if(!rec)
					continue;
				DrumSet set;
				set.program = static_cast<uint8_t>(program);
				if(!img.name(rec + 0x500, set.name))
				{
					_error = "The control ROM's drum set table is not one the editor knows.";
					return false;
				}
				map.drumSets.push_back(std::move(set));
			}

			// Every map has the 128 GM tones on bank 0 and STANDARD on program 1
			if(map.tones.size() < 128 || map.drumSets.empty() || map.drumSets.front().program != 0)
			{
				_error = "The control ROM's tone tables are not where the editor expects them.";
				return false;
			}

			std::sort(map.tones.begin(), map.tones.end(), [](const Tone& _a, const Tone& _b)
			{
				return std::pair{_a.program, _a.bankMsb} < std::pair{_b.program, _b.bankMsb};
			});
			_catalog.maps.push_back(std::move(map));
		}

		// No program substitutes for a missing tone. 40 1x 25 decides between keeping the tone the part had and
		// falling silent (1.01 00:2DC6, 1.02 00:3B18); the VE-GS Pro keeps it only below bank 64 and program 120
		// (00:3DFC). A missing drum set is always ignored (1.02 00:3CCC).
		_catalog.missingTone = _model == DeviceModel::VeGsPro ? MissingTone::KeepIfFlaggedBelow : MissingTone::KeepIfFlagged;
		_catalog.missingDrumSet = MissingDrumSet::Ignored;
		if(pro)
			_catalog.hiddenMaps = {117, 118, 119};
		return true;
	}
}
