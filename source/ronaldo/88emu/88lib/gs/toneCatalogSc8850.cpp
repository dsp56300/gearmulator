#include "88lib/gs/toneCatalog.h"

#include <algorithm>
#include <array>

// The SC-8850's and the SC-8820's tone tables. They sit at different addresses in the two images, so they are
// found where the firmware's own lookups load them, and the lookups are done the way the firmware does them. The
// code is the same instruction for instruction on both.
//
// Tone resolver (SC-8850 0x20A29A, SC-8820 0xE87F2A): map index = mapTab[map], row = varTab[index * 128 + bank],
// entry = toneMap[row * 128 + program], u16, FF and bit 15 for none. It compares the map with 123 first
// (CMP/EQ #123,R0; that map is gated by the board variant) and loads the gate function, the map table, the
// variation table (map table + 0x80) and the tone map as PC relative literals after it.
//
// An entry below 0x4000 is a two partial tone, 0x100 bytes each; 0x4000-0x5FFF a four partial tone, 0x1E8 bytes
// each; 0x6000-0x7FFF a tone layered of two others, 24 bytes each. Every record starts with its 12 character name.
// The record bases are in two bank descriptors the firmware fills at boot from literals (SC-8850 0x206490):
// MOV.L #ram,R4; MOV.L #a,R3; MOV.L #b,R2; MOV.L R3,@R4; MOV.L R2,@(4,R4), and so on up to RTS. The first has the
// two partial tones at +4, the drum sets at +0x10 and the layered tones at +0x14, the second the four partial tones
// at +4.
//
// Drum resolver (SC-8850 0x206D4E): EXTU.B R4,R4; MOV.L @(d,PC),R0; MOV.B @(R0,R4),R3 loads the drum map table,
// the drum slot table (map table + 0x80) and the drum set numbers: index = drumMapTab[map], slot =
// drumSlotTab[index * 128 + program], FF for none, set = u16 drumSets[slot]. A drum set is 0x50C bytes, its name
// at +0x500.
namespace emu88Lib::gs
{
	namespace
	{
		struct Image
		{
			const std::vector<RomRegion>& regions;

			const uint8_t* ptr(const uint32_t _a, const uint32_t _n = 1) const
			{
				for(const auto& r : regions)
				{
					if(_a >= r.base && _a - r.base + _n <= r.data.size())
						return r.data.data() + (_a - r.base);
				}
				return nullptr;
			}
			bool has(const uint32_t _a, const uint32_t _n = 1) const { return ptr(_a, _n) != nullptr; }
			uint8_t u8(const uint32_t _a) const { const auto* p = ptr(_a); return p ? *p : 0xff; }
			uint16_t u16(const uint32_t _a) const
			{
				const auto* p = ptr(_a, 2);
				return p ? static_cast<uint16_t>(p[0] << 8 | p[1]) : 0xffff;
			}
			uint32_t u32(const uint32_t _a) const { return static_cast<uint32_t>(u16(_a)) << 16 | u16(_a + 2); }

			bool name(const uint32_t _a, std::string& _name) const
			{
				const auto* p = ptr(_a, 12);
				if(!p)
					return false;
				_name.assign(reinterpret_cast<const char*>(p), 12);
				for(auto& c : _name)
				{
					if(c < 0x20 || c > 0x7e)
						return false;
				}
				while(!_name.empty() && _name.back() == ' ')
					_name.pop_back();
				return true;
			}

			// The literal MOV.L @(disp,PC),Rn at _a loads
			bool literal(const uint32_t _a, uint32_t& _value, uint32_t& _reg) const
			{
				const auto op = u16(_a);
				if((op & 0xf000) != 0xd000)
					return false;
				const auto at = ((_a + 4) & ~3u) + (op & 0xff) * 4;
				if(!has(at, 4))
					return false;
				_value = u32(at);
				_reg = (op >> 8) & 15;
				return true;
			}

			std::vector<uint32_t> literalsAfter(const uint32_t _a, const uint32_t _length) const
			{
				std::vector<uint32_t> result;
				for(auto p = _a; p < _a + _length; p += 2)
				{
					uint32_t v, r;
					if(literal(p, v, r))
						result.push_back(v);
				}
				return result;
			}

			template<typename F> void forEachInstruction(F&& _f) const
			{
				for(const auto& r : regions)
				{
					if(!r.code)
						continue;
					for(uint32_t o = 0; o + 2 <= r.data.size(); o += 2)
					{
						if(_f(r.base + o))
							return;
					}
				}
			}
		};

		struct Layout
		{
			uint32_t mapTab = 0, varTab = 0, toneMap = 0;
			uint32_t drumMapTab = 0, drumSlotTab = 0, drumSets = 0;
			uint32_t tones = 0, tones4 = 0, layered = 0, drumRecords = 0;
		};

		bool discover(const Image& _img, Layout& _l)
		{
			_img.forEachInstruction([&](const uint32_t _a)
			{
				if(_img.u16(_a) != 0x887b)	// CMP/EQ #123,R0
					return false;
				const auto lits = _img.literalsAfter(_a, 0x60);
				if(lits.size() < 4 || lits[0] >= 0x10000 || lits[2] != lits[1] + 0x80 || !_img.has(lits[3], 2))
					return false;
				_l.mapTab = lits[1];
				_l.varTab = lits[2];
				_l.toneMap = lits[3];
				return true;
			});

			_img.forEachInstruction([&](const uint32_t _a)
			{
				// EXTU.B R4,R4; MOV.L @(d,PC),R0; MOV.B @(R0,R4),R3
				if(_img.u16(_a) != 0x644c || (_img.u16(_a + 2) & 0xff00) != 0xd000 || _img.u16(_a + 4) != 0x034c)
					return false;
				std::vector<uint32_t> lits;
				for(const auto v : _img.literalsAfter(_a, 0x40))
				{
					if(v > 0x10000)
						lits.push_back(v);
				}
				if(lits.size() < 3 || lits[1] != lits[0] + 0x80)
					return false;
				_l.drumMapTab = lits[0];
				_l.drumSlotTab = lits[1];
				_l.drumSets = lits[2];
				return true;
			});

			// The two bank descriptors, replayed up to the RTS and its delay slot
			std::vector<std::array<uint32_t, 8>> descriptors;
			_img.forEachInstruction([&](const uint32_t _a)
			{
				if((_img.u16(_a) & 0xff00) != 0xd400 || (_img.u16(_a + 2) & 0xff00) != 0xd300 ||
				   (_img.u16(_a + 4) & 0xff00) != 0xd200 || _img.u16(_a + 6) != 0x2432 || _img.u16(_a + 8) != 0x1421)
					return false;
				std::array<uint32_t, 16> reg{};
				std::array<uint32_t, 8> fields{};
				bool rts = false;
				for(auto p = _a; p < _a + 0x40; p += 2)
				{
					const auto op = _img.u16(p);
					uint32_t v, r;
					if(_img.literal(p, v, r))
						reg[r] = v;
					else if((op & 0xff0f) == 0x2402)	// MOV.L Rm,@R4
						fields[0] = reg[(op >> 4) & 15];
					else if((op & 0xff00) == 0x1400 && (op & 15) < fields.size())	// MOV.L Rm,@(disp,R4)
						fields[op & 15] = reg[(op >> 4) & 15];
					if(rts)
						break;
					if(op == 0x000b)
						rts = true;
				}
				descriptors.push_back(fields);
				return descriptors.size() == 2;
			});

			if(!_l.mapTab || !_l.drumMapTab || descriptors.size() < 2)
				return false;
			_l.tones = descriptors[0][1];
			_l.drumRecords = descriptors[0][4];
			_l.layered = descriptors[0][5];
			_l.tones4 = descriptors[1][1];
			return true;
		}

		bool toneName(const Image& _img, const Layout& _l, const uint16_t _entry, std::string& _name)
		{
			if(_entry >= 0x6000)
				return _img.name(_l.layered + (_entry - 0x6000u) * 24u, _name);
			if(_entry >= 0x4000)
				return _img.name(_l.tones4 + (_entry - 0x4000u) * 0x1e8u, _name);
			return _img.name(_l.tones + _entry * 0x100u, _name);
		}
	}

	bool extractSc8850Catalog(const std::vector<RomRegion>& _regions, const DeviceModel _model, ToneCatalog& _catalog,
		std::string& _error)
	{
		const Image img{_regions};
		Layout l;
		if(!discover(img, l))
		{
			_error = "The firmware's tone lookup was not found in its ROMs.";
			return false;
		}

		// The maps a part can be set to. 117 and 118 hold the halves of the layered tones, 119 is XG mode's, 122
		// GM2's (bank 121 and 120) and 123 a board variant's; none of them is listed. There are no user tones on the
		// SC-55 map.
		const std::pair<uint8_t, const char*> mapNames[] = {
			{1, "SC-55"}, {2, "SC-88"}, {3, "SC-88Pro"}, {4, _model == DeviceModel::Sc8820 ? "SC-8820" : "SC-8850"}};

		_catalog.maps.clear();
		for(const auto& [number, name] : mapNames)
		{
			ToneMap map;
			map.number = number;
			map.name = name;
			map.userTones = number != 1;

			const auto index = img.u8(l.mapTab + number);
			if(index == 0xff)
			{
				_error = std::string("The firmware has no ") + name + " map.";
				return false;
			}
			for(uint32_t bank = 0; bank < 128; ++bank)
			{
				const auto row = img.u8(l.varTab + index * 128u + bank);
				if(row == 0xff)
					continue;
				for(uint32_t program = 0; program < 128; ++program)
				{
					const auto entry = img.u16(l.toneMap + row * 256u + program * 2u);
					if(entry & 0x8000)
						continue;
					Tone tone;
					tone.bankMsb = static_cast<uint8_t>(bank);
					tone.program = static_cast<uint8_t>(program);
					if(!toneName(img, l, entry, tone.name))
					{
						_error = "The firmware's tone records are not where the editor expects them.";
						return false;
					}
					map.tones.push_back(std::move(tone));
				}
			}

			const auto drumIndex = img.u8(l.drumMapTab + number);
			for(uint32_t program = 0; program < 128 && drumIndex != 0xff; ++program)
			{
				const auto slot = img.u8(l.drumSlotTab + drumIndex * 128u + program);
				if(slot == 0xff)
					continue;
				DrumSet set;
				set.program = static_cast<uint8_t>(program);
				if(!img.name(l.drumRecords + img.u16(l.drumSets + slot * 2u) * 0x50cu + 0x500u, set.name))
				{
					_error = "The firmware's drum sets are not where the editor expects them.";
					return false;
				}
				map.drumSets.push_back(std::move(set));
			}

			if(map.tones.size() < 128 || map.drumSets.empty() || map.drumSets.front().program != 0)
			{
				_error = "The firmware's tone tables are not where the editor expects them.";
				return false;
			}
			std::sort(map.tones.begin(), map.tones.end(), [](const Tone& _a, const Tone& _b)
			{
				return std::pair{_a.program, _a.bankMsb} < std::pair{_b.program, _b.bankMsb};
			});
			_catalog.maps.push_back(std::move(map));
		}

		// No substitution: with 40 1x 25 set, its default, a program change for nothing is refused, but the bank
		// select LSB it brought stays (0x20A0C8); with it clear the part falls silent. An empty drum set program
		// is refused.
		_catalog.missingTone = MissingTone::KeepIfFlagged;
		_catalog.missingDrumSet = MissingDrumSet::Ignored;
		_catalog.ignoreRestoresMap = false;
		// 117 and 118 hold nothing a part can select by itself; 123 is gated by the board variant, which ours is not
		_catalog.hiddenMaps = {119, 122};
		return true;
	}
}
