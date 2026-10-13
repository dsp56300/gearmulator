#include "88lib/mt32/memory.h"

#include "88lib/mt32/params.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace emu88Lib::mt32
{
	namespace
	{
		// ---- what the boards hold after a reset, read back from the emulated firmware

		constexpr uint8_t g_laSystemDefault[g_laSystemSize] = {
			0x4a, 0, 5, 3,					// master tune, reverb mode, time, level
			3, 10, 6, 4, 3, 0, 0, 0, 6,		// partial reserve
			1, 2, 3, 4, 5, 6, 7, 8, 9,		// MIDI channels 2-10
			100,
		};

		// The parts' timbre group, number and pan; the rest of a patch temp is the same for all
		struct PartDefault { uint8_t group, number, pan; };
		constexpr PartDefault g_laPartDefaults[g_laParts] = {
			{1, 4, 7}, {0, 48, 8}, {1, 31, 7}, {1, 14, 8}, {0, 41, 4}, {0, 3, 10}, {1, 46, 0}, {1, 58, 14}, {0, 0, 0},
		};

		// The rhythm setup of keys 35-76 (entries 11-51), timbre and pan; level 100 and reverb on. 0 for an empty key.
		struct RhythmDefault { uint8_t timbre, pan; };
		constexpr RhythmDefault g_rhythmDefaults[] = {
			{0x40, 7}, {0x40, 7}, {0x4a, 6}, {0x41, 7}, {0x4b, 8}, {0x45, 6}, {0x44, 11}, {0x46, 6}, {0x44, 11},
			{0x5d, 6}, {0x43, 8}, {0x47, 6}, {0x43, 8}, {0x42, 3}, {0x48, 6}, {0x42, 3}, {0x49, 8}, {0, 0}, {0, 0},
			{0x56, 9}, {0, 0}, {0x4c, 7}, {0, 0}, {0, 0}, {0, 0}, {0x52, 2}, {0x53, 4}, {0x4d, 8}, {0x4e, 9},
			{0x4f, 10}, {0x50, 7}, {0x51, 5}, {0x54, 2}, {0x55, 2}, {0x5b, 9}, {0x58, 4}, {0x5a, 9}, {0x59, 9},
			{0x5c, 10}, {0, 0}, {0x57, 12},
		};
		static_assert(std::size(g_rhythmDefaults) == 41);
		constexpr size_t g_firstRhythmDefault = 11;
		// From key 76 on the CM-32L plays its sound effects, r31-r63 in a row
		constexpr size_t g_firstCmEffectKey = 52;

		constexpr uint8_t g_pcmSystemDefault[g_pcmSystemSize] = {
			0x40, 0, 5, 3,
			2, 8, 21, 0, 0, 0,
			10, 11, 12, 13, 14, 15,			// MIDI channels 11-16
			100,
		};

		// The parts' tone number, LFO rate and pan; tone media internal
		struct PcmPartDefault { uint8_t tone, lfoRate, pan; };
		constexpr PcmPartDefault g_pcmPartDefaults[g_pcmParts] = {
			{0x28, 0, 0x40}, {0x2b, 50, 0x51}, {0x00, 50, 0x40}, {0x34, 50, 0x63}, {0x14, 50, 0x1b}, {0x40, 50, 0x2d},
		};
		// A patch from offset 02 on, the factory patches' and the patch temps'
		constexpr uint8_t g_pcmPatchDefault[g_pcmPatchMemorySize - 2] = {
			0x0c, 0x32, 0x0c, 0x00, 0x7f, 0x00, 0x01, 0x0f, 0x40, 0x40, 0x32, 0x00, 0x00, 0x00, 0x02, 0x04, 0x0c,
		};
		// The internal tones of factory patches 1-64. Patches 65-128 play card tones 1-64. The firmware keeps the
		// table in its ROM (at B454), and after power-on or a reset a program change reads it from there; the next
		// write to the patch memory copies it over the patch memory before it takes the write, so what the patch
		// memory had before the reset is gone either way. That copy gets patches 65-128 wrong, see
		// pcmInitialisedPatchMemory().
		constexpr uint8_t g_pcmFactoryTones[64] = {
			0, 1, 2, 3, 4, 6, 8, 10, 12, 14, 15, 17, 18, 20, 21, 26, 27, 28, 29, 32, 33, 34, 35, 36, 37, 38, 39, 40,
			42, 43, 44, 45, 46, 47, 48, 49, 50, 52, 54, 56, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68, 69, 70, 71, 72,
			73, 74, 75, 77, 78, 79, 80, 81, 82, 83,
		};

		uint16_t presetKey(const TimbreSource _s) { return static_cast<uint16_t>(_s.group << 8 | _s.number); }
		uint16_t romMemoryKey(const size_t _slot) { return static_cast<uint16_t>(0x8000 | _slot); }

		bool isTimbreArea(const Area _area) { return _area == Area::TimbreTemp || _area == Area::TimbreMemory; }

		bool storeTimbreByte(const size_t _offset, const LaFirmware _firmware, uint8_t& _value)
		{
			if(_offset < g_timbreCommonSize)
				return storeByte(Block::TimbreCommon, static_cast<uint8_t>(_offset), _firmware, _value);
			return storeByte(Block::Partial, static_cast<uint8_t>((_offset - g_timbreCommonSize) % g_partialSize),
				_firmware, _value);
		}

		// The LA board truncates, the CM-32P rounds
		uint8_t ccToLevel(const uint8_t _value) { return static_cast<uint8_t>(_value * 100 / 127); }
		uint8_t ccToLevelPcm(const uint8_t _value) { return static_cast<uint8_t>((_value * 100 + 63) / 127); }

		// The CM-32P has 84 internal tones. A patch temp set to internal media and a tone past them gets tone 85
		// instead, whatever wrote it; the patch memory keeps such a tone as it is.
		constexpr uint8_t g_pcmInternalTones = 84;
		constexpr uint8_t g_pcmNoTone = 85;

		uint8_t ccToPanLa(const uint8_t _value, const LaFirmware _firmware)
		{
			if(_firmware == LaFirmware::Old)
				return static_cast<uint8_t>(std::min(_value / 9, 14));
			return static_cast<uint8_t>((_value * 15 + 7) >> 7);
		}

		// What the CM-32P's patch memory holds once its firmware copied the factory patches to it, as the copy loop
		// at 5809 has it: patches 1-64 from the ROM, then for the card patches the 19 byte template at B914 (patch
		// 65, media card, tone 1) and after it media and tone for the next patch - followed by the whole template
		// again, its media and tone included, instead of the 17 bytes after them. So patch 66 on are 21 bytes apart
		// and run over each other's places. A bug of the firmware: the dump is the catalogued 1.00 program.
		const std::array<uint8_t, g_pcmPatchMemoryCount * g_pcmPatchMemorySize>& pcmInitialisedPatchMemory()
		{
			static const auto memory = []
			{
				std::array<uint8_t, g_pcmPatchMemoryCount * g_pcmPatchMemorySize> m{};
				for(size_t n = 0; n < 64; ++n)
				{
					const auto at = m.begin() + static_cast<ptrdiff_t>(n * g_pcmPatchMemorySize);
					at[1] = g_pcmFactoryTones[n];
					std::copy(std::begin(g_pcmPatchDefault), std::end(g_pcmPatchDefault), at + 2);
				}
				std::array<uint8_t, g_pcmPatchMemorySize> templ{1, 0};
				std::copy(std::begin(g_pcmPatchDefault), std::end(g_pcmPatchDefault), templ.begin() + 2);
				constexpr size_t end = g_pcmPatchMemoryCount * g_pcmPatchMemorySize;
				size_t at = 64 * g_pcmPatchMemorySize, i = 0;
				uint8_t tone = 0;
				while(true)
				{
					m[at] = templ[i];
					if(++i != templ.size())
					{
						if(++at == end)
							break;
						continue;
					}
					i = 0;
					if(++at == end)
						break;
					m[at++] = 1;
					m[at++] = ++tone;
				}
				return m;
			}();
			return memory;
		}

		// SysEx data at an address, kept in emission order; consecutive runs merge before they are split into
		// messages
		struct Segment
		{
			uint32_t address;
			std::vector<uint8_t> data;
		};

		void addSegment(std::vector<Segment>& _segments, const uint32_t _address, const uint8_t* _data,
			const size_t _size, const bool _separate = false)
		{
			if(!_size)
				return;
			if(!_segments.empty() && !_separate)
			{
				auto& last = _segments.back();
				if(toLinear(last.address) + last.data.size() == toLinear(_address))
				{
					last.data.insert(last.data.end(), _data, _data + _size);
					return;
				}
			}
			_segments.push_back({_address, std::vector<uint8_t>(_data, _data + _size)});
		}
	}

	Memory::Memory(const DeviceModel _model)
		: m_model(_model)
		, m_firmware(mt32::laFirmware(_model))
		, m_hasLa(hasLaBoard(_model))
		, m_hasPcm(hasPcmBoard(_model))
	{
		for(size_t a = 0; a < m_areas.size(); ++a)
		{
			const auto area = static_cast<Area>(a);
			const bool present = areaInfo(area).board == Board::La ? m_hasLa : m_hasPcm;
			const size_t size = present ? static_cast<size_t>(entryCount(area)) * areaInfo(area).entrySize : 0;
			m_areas[a].values.assign(size, 0);
			m_areas[a].known.assign(size, 0);
			m_areas[a].touched.assign(size, 0);
		}
		reset();
	}

	uint16_t Memory::entryCount(const Area _area) const
	{
		return mt32::entryCount(_area, m_firmware);
	}

	size_t Memory::index(const Area _area, const size_t _entry, const size_t _offset) const
	{
		return _entry * areaInfo(_area).entrySize + _offset;
	}

	uint8_t Memory::value(const Area _area, const size_t _entry, const size_t _offset) const
	{
		return data(_area).values[index(_area, _entry, _offset)];
	}

	bool Memory::known(const Area _area, const size_t _entry, const size_t _offset) const
	{
		return data(_area).known[index(_area, _entry, _offset)] != 0;
	}

	bool Memory::entryKnown(const Area _area, const size_t _entry) const
	{
		const auto& d = data(_area);
		const auto begin = d.known.begin() + static_cast<ptrdiff_t>(index(_area, _entry, 0));
		return std::all_of(begin, begin + areaInfo(_area).entrySize, [](const uint8_t _k) { return _k != 0; });
	}

	const uint8_t* Memory::entry(const Area _area, const size_t _entry) const
	{
		return data(_area).values.data() + index(_area, _entry, 0);
	}

	uint8_t Memory::laChannel(const size_t _part) const
	{
		return value(Area::LaSystem, 0, 0x0d + _part);
	}

	uint8_t Memory::pcmChannel(const size_t _part) const
	{
		return value(Area::PcmSystem, 0, 0x0a + _part);
	}

	const std::array<uint8_t, g_timbreSize>* Memory::preset(const TimbreSource _source) const
	{
		const auto it = m_presets.find(presetKey(_source));
		return it == m_presets.end() ? nullptr : &it->second;
	}

	bool Memory::set(const Area _area, const size_t _entry, const size_t _offset, const uint8_t _value,
		const bool _known)
	{
		auto& d = data(_area);
		const auto i = index(_area, _entry, _offset);
		const bool changed = d.values[i] != _value || d.known[i] != static_cast<uint8_t>(_known);
		d.values[i] = _value;
		d.known[i] = _known;
		return changed;
	}

	void Memory::reset()
	{
		for(auto& a : m_areas)
		{
			std::fill(a.values.begin(), a.values.end(), 0);
			std::fill(a.known.begin(), a.known.end(), 1);
			std::fill(a.touched.begin(), a.touched.end(), 0);
		}
		m_rpnLa = {};
		m_rpnPcm = {};
		if(m_hasLa)
			resetLa();
		if(m_hasPcm)
			resetPcm();
	}

	void Memory::allParametersReset()
	{
		const auto masterVolume = m_hasLa ? value(Area::LaSystem, 0, 0x16) : 0;
		const auto receiveChannels = m_laReceiveChannels;
		const auto rpnPcm = m_rpnPcm;
		reset();
		m_rpnPcm = rpnPcm;
		if(m_hasLa)
			set(Area::LaSystem, 0, 0x16, masterVolume);
		if(m_hasLa && m_firmware == LaFirmware::Old)
			m_laReceiveChannels = receiveChannels;
	}

	void Memory::resetLa()
	{
		for(size_t i = 0; i < g_laSystemSize; ++i)
			set(Area::LaSystem, 0, i, g_laSystemDefault[i]);
		for(size_t p = 0; p < g_laParts; ++p)
			m_laReceiveChannels[p] = laChannel(p);

		for(size_t p = 0; p < g_laParts; ++p)
		{
			const auto& d = g_laPartDefaults[p];
			const uint8_t patch[g_patchTempSize] = {d.group, d.number, 24, 50, 12, 0, 1, 0, 80, d.pan, 0,
				static_cast<uint8_t>(m_firmware == LaFirmware::Old ? 0 : 0x7f), 0, 0, 0, 0};
			for(size_t i = 0; i < g_patchTempSize; ++i)
				set(Area::PatchTemp, p, i, patch[i]);
		}

		const bool cm = m_model != DeviceModel::Mt32Old && m_model != DeviceModel::Mt32New;
		for(size_t k = 0; k < entryCount(Area::RhythmSetup); ++k)
		{
			uint8_t key[g_rhythmKeySize] = {0x7f, 100, 7, 1};
			if(k >= g_firstRhythmDefault && k - g_firstRhythmDefault < std::size(g_rhythmDefaults) &&
			   g_rhythmDefaults[k - g_firstRhythmDefault].timbre)
			{
				key[0] = g_rhythmDefaults[k - g_firstRhythmDefault].timbre;
				key[2] = g_rhythmDefaults[k - g_firstRhythmDefault].pan;
			}
			else if(cm && k >= g_firstCmEffectKey)
			{
				key[0] = static_cast<uint8_t>(0x5e + (k - g_firstCmEffectKey));
			}
			else if(cm)
			{
				key[1] = 0;
				key[3] = 0;
			}
			for(size_t i = 0; i < g_rhythmKeySize; ++i)
				set(Area::RhythmSetup, k, i, key[i]);
		}

		for(size_t n = 0; n < g_patchMemoryCount; ++n)
		{
			const uint8_t patch[g_patchMemorySize] = {static_cast<uint8_t>(n / 64), static_cast<uint8_t>(n % 64), 24,
				50, 12, 0, 1, 0};
			for(size_t i = 0; i < g_patchMemorySize; ++i)
				set(Area::PatchMemory, n, i, patch[i]);
		}

		// The new firmware clears the timbre memory, the old one fills it from a table in its ROM: timbre 1 is
		// "Slap Bass1", 2 "Str Sect 1", 64 "Orche Hit"
		for(size_t n = 0; n < g_timbreMemoryCount; ++n)
		{
			m_timbreMemoryFromRom[n] = m_firmware == LaFirmware::Old;
			if(!m_timbreMemoryFromRom[n])
				continue;
			const auto it = m_presets.find(romMemoryKey(n));
			for(size_t i = 0; i < g_timbreSize; ++i)
				set(Area::TimbreMemory, n, i, it != m_presets.end() ? it->second[i] : 0, it != m_presets.end());
		}

		for(size_t p = 0; p < g_laTimbreParts; ++p)
			loadTimbre(p);
	}

	void Memory::resetPcm()
	{
		for(size_t i = 0; i < g_pcmSystemSize; ++i)
			set(Area::PcmSystem, 0, i, g_pcmSystemDefault[i]);

		for(size_t p = 0; p < g_pcmParts; ++p)
		{
			const auto& d = g_pcmPartDefaults[p];
			set(Area::PcmPatchTemp, p, 0, 0);
			set(Area::PcmPatchTemp, p, 1, d.tone);
			for(size_t i = 2; i < g_pcmPatchMemorySize; ++i)
				set(Area::PcmPatchTemp, p, i, g_pcmPatchDefault[i - 2]);
			set(Area::PcmPatchTemp, p, 0x0c, d.lfoRate);
			set(Area::PcmPatchTemp, p, 0x13, d.pan);
			set(Area::PcmPatchTemp, p, 0x14, 80);
		}

		// The factory patches as a program change reads them from the ROM, card patches right
		for(size_t n = 0; n < g_pcmPatchMemoryCount; ++n)
		{
			set(Area::PcmPatchMemory, n, 0, n < 64 ? 0 : 1);
			set(Area::PcmPatchMemory, n, 1, n < 64 ? g_pcmFactoryTones[n] : static_cast<uint8_t>(n - 64));
			for(size_t i = 2; i < g_pcmPatchMemorySize; ++i)
				set(Area::PcmPatchMemory, n, i, g_pcmPatchDefault[i - 2]);
		}
		m_pcmPatchMemoryInRam = false;
	}

	void Memory::loadTimbre(const size_t _part)
	{
		const TimbreSource source{value(Area::PatchTemp, _part, 0), value(Area::PatchTemp, _part, 1)};
		m_timbreSources[_part] = source;
		auto& d = data(Area::TimbreTemp);
		std::fill_n(d.touched.begin() + static_cast<ptrdiff_t>(index(Area::TimbreTemp, _part, 0)), g_timbreSize, 0);
		if(source.group == 2)
		{
			for(size_t i = 0; i < g_timbreSize; ++i)
			{
				set(Area::TimbreTemp, _part, i, value(Area::TimbreMemory, source.number, i),
					known(Area::TimbreMemory, source.number, i));
			}
			return;
		}
		const auto* p = preset(source);
		for(size_t i = 0; i < g_timbreSize; ++i)
			set(Area::TimbreTemp, _part, i, p ? (*p)[i] : 0, p != nullptr);
	}

	bool Memory::storeAt(const Location& _location, uint8_t _value)
	{
		const auto area = _location.area;
		bool store;
		if(isTimbreArea(area))
			store = storeTimbreByte(_location.offset, m_firmware, _value);
		else
			store = storeByte(blockOf(area), static_cast<uint8_t>(_location.offset), m_firmware, _value);
		if(!store)
			return false;
		data(area).touched[index(area, _location.entry, _location.offset)] = 1;
		if(area == Area::TimbreMemory)
			m_timbreMemoryFromRom[_location.entry] = false;
		return set(area, _location.entry, _location.offset, _value);
	}

	bool Memory::applyReserves(const Area _area, const std::array<int, 9>& _written, const size_t _count,
		const uint8_t _total)
	{
		const size_t first = 4;
		bool changed = false;
		bool cut = false;
		int remaining = _total;
		for(size_t i = 0; i < _count; ++i)
		{
			int v = _written[i] >= 0 ? std::min<int>(_written[i], _total) : value(_area, 0, first + i);
			cut |= v > remaining;
			v = std::min(v, remaining);
			remaining -= v;
			if(_written[i] >= 0)
				data(_area).touched[index(_area, 0, first + i)] = 1;
			changed |= set(_area, 0, first + i, static_cast<uint8_t>(v));
		}
		// The old firmware's cut clears one byte too many: the next one, part 1's MIDI channel
		if(cut && _area == Area::LaSystem && m_firmware == LaFirmware::Old)
			changed |= set(_area, 0, first + _count, 0);
		return changed;
	}

	bool Memory::writeLa(const uint32_t _address, const uint8_t* _data, const size_t _size)
	{
		// The old firmware takes the partial reserves only all nine at once. A write that starts among them but
		// does not cover them all skips them - and puts its bytes after them from its first byte on, as many as are
		// left after the reserves it skipped: 10 00 0C with four bytes writes the first three to 10 00 0D-0F.
		if(m_firmware == LaFirmware::Old && (_address >> 16) == (g_laSystem >> 16))
		{
			const auto start = toLinear(_address) - toLinear(g_laSystem);
			constexpr uint32_t first = 4, end = 4 + g_laParts;
			if(start >= first && start < end && !(start == first && _size >= g_laParts))
			{
				const auto skipped = end - start;
				return writeLa(addressAdd(g_laSystem, end), _data, _size > skipped ? _size - skipped : 0);
			}
		}

		bool changed = false;
		std::array<int, 9> reserves;
		reserves.fill(-1);
		size_t reservesWritten = 0;
		std::array<bool, g_laTimbreParts> patchWrite{}, selectionWrite{}, numberWrite{};
		int firstPart = -1;
		bool systemWrite = (_address >> 16) == (g_laSystem >> 16);

		for(size_t i = 0; i < _size; ++i)
		{
			const auto loc = locate(Board::La, addressAdd(_address, static_cast<uint32_t>(i)), m_firmware);
			if(!loc.valid())
				continue;
			if(loc.area == Area::LaSystem && loc.offset >= 4 && loc.offset < 4 + g_laParts)
			{
				reserves[loc.offset - 4] = _data[i] & 0x7f;
				++reservesWritten;
				continue;
			}
			changed |= storeAt(loc, _data[i]);
			if(loc.area == Area::PatchTemp && loc.entry < g_laTimbreParts)
			{
				patchWrite[loc.entry] = true;
				selectionWrite[loc.entry] |= loc.offset <= 1;
				numberWrite[loc.entry] |= loc.offset == 1;
				if(firstPart < 0)
					firstPart = loc.entry;
			}
		}

		// The new firmware takes any of the partial reserves and cuts the parts after one off where the total
		// passes 32; the old one takes them only all nine at once, then cuts the same way
		if(reservesWritten && (m_firmware == LaFirmware::New || reservesWritten == g_laParts))
			changed |= applyReserves(Area::LaSystem, reserves, g_laParts, 32);

		// A write to the system area sets the parts' channels up again from it, even one the firmware ignores
		if(systemWrite || m_firmware == LaFirmware::New)
		{
			for(size_t p = 0; p < g_laParts; ++p)
				m_laReceiveChannels[p] = laChannel(p);
		}

		// A part takes its timbre again when its patch temp selects one. The old firmware does it at any write to
		// the patch temp. The new one when the group or the number is written for the part the message starts in;
		// for a part after it the CM-32L wants the number, the MT-32 2.x does it for no part after it at all - those
		// keep their timbre whatever their patch temp says now.
		for(size_t p = 0; p < g_laTimbreParts; ++p)
		{
			bool load = patchWrite[p];
			if(m_firmware == LaFirmware::New)
			{
				if(static_cast<int>(p) == firstPart)
					load = selectionWrite[p];
				else
					load = m_model != DeviceModel::Mt32New && numberWrite[p];
			}
			if(load)
			{
				loadTimbre(p);
				changed = true;
			}
		}
		return changed;
	}

	bool Memory::writeChannelTimbre(const uint8_t _channel, const uint32_t _offset, const uint8_t* _data,
		const size_t _size)
	{
		// The first part on the channel. The new firmware keeps a write to it within its timbre, the old one goes
		// on into the next part's.
		size_t part = g_laTimbreParts;
		for(size_t p = 0; p < g_laTimbreParts && part == g_laTimbreParts; ++p)
		{
			if(laReceiveChannel(p) == _channel)
				part = p;
		}
		if(part == g_laTimbreParts)
			return false;
		bool changed = false;
		for(size_t i = 0; i < _size; ++i)
		{
			const size_t offset = _offset + i;
			const size_t target = part + offset / g_timbreSize;
			if(m_firmware == LaFirmware::New ? offset >= g_timbreSize : target >= g_laTimbreParts)
				continue;
			changed |= storeAt({Area::TimbreTemp, static_cast<uint16_t>(target),
				static_cast<uint16_t>(offset % g_timbreSize)}, _data[i]);
		}
		return changed;
	}

	bool Memory::writePcm(const uint32_t _address, const uint8_t* _data, const size_t _size)
	{
		bool changed = false;
		std::array<int, g_pcmParts> reserves;
		reserves.fill(-1);
		bool systemWrite = false;
		std::array<bool, g_pcmParts> patchWrite{};
		for(size_t i = 0; i < _size; ++i)
		{
			const auto loc = locate(Board::Pcm, addressAdd(_address, static_cast<uint32_t>(i)), m_firmware);
			if(!loc.valid())
				continue;
			systemWrite |= loc.area == Area::PcmSystem;
			if(loc.area == Area::PcmSystem && loc.offset >= 4 && loc.offset < 4 + g_pcmParts)
			{
				reserves[loc.offset - 4] = _data[i] & 0x7f;
				continue;
			}
			if(loc.area == Area::PcmPatchMemory && !m_pcmPatchMemoryInRam)
			{
				const auto& initialised = pcmInitialisedPatchMemory();
				for(size_t b = 0; b < initialised.size(); ++b)
					set(Area::PcmPatchMemory, b / g_pcmPatchMemorySize, b % g_pcmPatchMemorySize, initialised[b]);
				m_pcmPatchMemoryInRam = true;
				changed = true;
			}
			changed |= storeAt(loc, _data[i]);
			if(loc.area == Area::PcmPatchTemp)
				patchWrite[loc.entry] = true;
		}
		for(size_t p = 0; p < g_pcmParts; ++p)
		{
			if(patchWrite[p])
				changed |= checkPcmTone(p);
		}
		// The manual wants all six reserves at once, at most 31 in all. The firmware's check after any write to the
		// system area (at 593F) leaves part 1's at 31 at most, and its loop over the others branches into the code
		// that clears them instead of back: whatever was written, parts 2-6 have no reserve.
		if(systemWrite)
		{
			for(size_t p = 0; p < g_pcmParts; ++p)
			{
				if(reserves[p] >= 0)
					data(Area::PcmSystem).touched[index(Area::PcmSystem, 0, 4 + p)] = 1;
			}
			const auto first = reserves[0] >= 0 ? reserves[0] : value(Area::PcmSystem, 0, 4);
			changed |= set(Area::PcmSystem, 0, 4, static_cast<uint8_t>(std::min(first, 31)));
			for(size_t p = 1; p < g_pcmParts; ++p)
				changed |= set(Area::PcmSystem, 0, 4 + p, 0);
		}
		return changed;
	}

	bool Memory::processDt1(const Dt1& _dt1)
	{
		const auto top = static_cast<uint8_t>(_dt1.address >> 16);
		if(_dt1.deviceId == g_unitDeviceId)
		{
			if(top == 0x7f)
			{
				allParametersReset();
				return true;
			}
			bool changed = false;
			if(m_hasLa)
				changed |= writeLa(_dt1.address, _dt1.data, _dt1.size);
			if(m_hasPcm)
				changed |= writePcm(_dt1.address, _dt1.data, _dt1.size);
			return changed;
		}
		// A part's timbre temp by its channel. The PCM board takes no device ID but its unit's.
		if(_dt1.deviceId < 16 && m_hasLa && top == (g_channelTimbreTemp >> 16))
			return writeChannelTimbre(_dt1.deviceId, toLinear(_dt1.address) - toLinear(g_channelTimbreTemp),
				_dt1.data, _dt1.size);
		return false;
	}

	bool Memory::processSysex(const uint8_t* _data, const size_t _size)
	{
		Dt1 dt1;
		if(!parseDt1(_data, _size, dt1))
			return false;
		return processDt1(dt1);
	}

	void Memory::programChangeLa(const size_t _part, const uint8_t _program)
	{
		// The patch memory's bytes over the patch temp's; output level and pan stay. The old firmware stores no
		// byte 07.
		const size_t count = m_firmware == LaFirmware::Old ? 7 : g_patchMemorySize;
		for(size_t i = 0; i < count; ++i)
			set(Area::PatchTemp, _part, i, value(Area::PatchMemory, _program & 0x7f, i));
		loadTimbre(_part);
	}

	void Memory::programChangePcm(const size_t _part, const uint8_t _program)
	{
		for(size_t i = 0; i < g_pcmPatchMemorySize; ++i)
			set(Area::PcmPatchTemp, _part, i, value(Area::PcmPatchMemory, _program & 0x7f, i));
		checkPcmTone(_part);
	}

	bool Memory::checkPcmTone(const size_t _part)
	{
		if(value(Area::PcmPatchTemp, _part, 0) || value(Area::PcmPatchTemp, _part, 1) < g_pcmInternalTones)
			return false;
		return set(Area::PcmPatchTemp, _part, 1, g_pcmNoTone);
	}

	bool Memory::processShort(const uint8_t _status, const uint8_t _data1, const uint8_t _data2)
	{
		const auto type = static_cast<uint8_t>(_status & 0xf0);
		const auto channel = static_cast<uint8_t>(_status & 0x0f);
		bool changed = false;

		// The parts on the channel: on the LA board the eight melodic ones, plus the rhythm part for anything but
		// a program change, which it ignores
		const auto forParts = [&](const bool _withRhythm, const auto& _laFunc, const auto& _pcmFunc)
		{
			if(m_hasLa)
			{
				for(size_t p = 0; p < (_withRhythm ? g_laParts : g_laTimbreParts); ++p)
				{
					if(laReceiveChannel(p) == channel)
					{
						_laFunc(p);
						changed = true;
					}
				}
			}
			if(m_hasPcm)
			{
				for(size_t p = 0; p < g_pcmParts; ++p)
				{
					if(pcmChannel(p) == channel)
					{
						_pcmFunc(p);
						changed = true;
					}
				}
			}
		};

		if(type == 0xc0)
		{
			forParts(false, [&](const size_t _p) { programChangeLa(_p, _data1); },
				[&](const size_t _p) { programChangePcm(_p, _data1); });
			return changed;
		}
		if(type != 0xb0)
			return false;

		const auto select = [&](RpnState& _rpn)
		{
			if(_data1 == 101)
				_rpn.msb = _data2;
			else if(_data1 == 100)
				_rpn.lsb = _data2;
			_rpn.nrpn = _data1 == 99 || _data1 == 98;
		};
		const auto bender = [](const RpnState& _rpn) { return !_rpn.nrpn && !_rpn.msb && !_rpn.lsb; };
		switch(_data1)
		{
		case 7:
			forParts(true, [&](const size_t _p) { set(Area::PatchTemp, _p, 0x08, ccToLevel(_data2)); },
				[&](const size_t _p) { set(Area::PcmPatchTemp, _p, 0x14, ccToLevelPcm(_data2)); });
			break;
		case 10:
			forParts(true, [&](const size_t _p) { set(Area::PatchTemp, _p, 0x09, ccToPanLa(_data2, m_firmware)); },
				[&](const size_t _p) { set(Area::PcmPatchTemp, _p, 0x13, _data2); });
			break;
		case 101:
		case 100:
		case 99:
		case 98:
			forParts(true, [&](const size_t _p) { select(m_rpnLa[_p]); }, [&](const size_t _p) { select(m_rpnPcm[_p]); });
			break;
		case 6:
			// RPN 0, the bender range. The old LA firmware takes no RPN.
			forParts(true, [&](const size_t _p)
				{
					if(m_firmware == LaFirmware::New && bender(m_rpnLa[_p]))
						set(Area::PatchTemp, _p, 0x04, std::min<uint8_t>(_data2, 24));
				},
				[&](const size_t _p)
				{
					if(bender(m_rpnPcm[_p]))
						set(Area::PcmPatchTemp, _p, 0x04, std::min<uint8_t>(_data2, 12));
				});
			break;
		default:
			break;
		}
		return changed;
	}

	void Memory::addPreset(const TimbreSource _source, const std::array<uint8_t, g_timbreSize>& _data)
	{
		if(!m_hasLa || _source.group == 2)
			return;
		m_presets[presetKey(_source)] = _data;
		for(size_t p = 0; p < g_laTimbreParts; ++p)
		{
			if(!(m_timbreSources[p] == _source))
				continue;
			for(size_t i = 0; i < g_timbreSize; ++i)
			{
				if(!known(Area::TimbreTemp, p, i))
					set(Area::TimbreTemp, p, i, _data[i]);
			}
		}
	}

	void Memory::learnPreset(const size_t _part)
	{
		const auto source = m_timbreSources[_part];
		if(source.group == 2 || !entryKnown(Area::TimbreTemp, _part))
			return;
		const auto& d = data(Area::TimbreTemp);
		const auto begin = d.touched.begin() + static_cast<ptrdiff_t>(index(Area::TimbreTemp, _part, 0));
		if(std::any_of(begin, begin + g_timbreSize, [](const uint8_t _t) { return _t != 0; }))
			return;
		auto& p = m_presets[presetKey(source)];
		std::memcpy(p.data(), entry(Area::TimbreTemp, _part), g_timbreSize);
	}

	bool Memory::processReply(const uint8_t* _data, const size_t _size)
	{
		Dt1 dt1;
		if(!m_hasLa || !parseDt1(_data, _size, dt1) || dt1.deviceId != g_unitDeviceId)
			return false;
		bool changed = false;
		for(size_t i = 0; i < dt1.size; ++i)
		{
			const auto loc = locate(Board::La, addressAdd(dt1.address, static_cast<uint32_t>(i)), m_firmware);
			if(loc.valid())
				changed |= set(loc.area, loc.entry, loc.offset, dt1.data[i] & 0x7f);
		}
		if(!changed)
			return false;

		// A preset seen whole is known from now on, wherever it is
		for(size_t p = 0; p < g_laTimbreParts; ++p)
			learnPreset(p);
		for(size_t n = 0; n < g_timbreMemoryCount; ++n)
		{
			if(m_timbreMemoryFromRom[n] && entryKnown(Area::TimbreMemory, n))
				std::memcpy(m_presets[romMemoryKey(n)].data(), entry(Area::TimbreMemory, n), g_timbreSize);
		}
		const auto fill = [&](const Area _area, const size_t _entry, const uint16_t _key)
		{
			const auto it = m_presets.find(_key);
			if(it == m_presets.end())
				return;
			for(size_t i = 0; i < g_timbreSize; ++i)
			{
				if(!known(_area, _entry, i))
					set(_area, _entry, i, it->second[i]);
			}
		};
		for(size_t p = 0; p < g_laTimbreParts; ++p)
		{
			if(m_timbreSources[p].group != 2)
				fill(Area::TimbreTemp, p, presetKey(m_timbreSources[p]));
		}
		for(size_t n = 0; n < g_timbreMemoryCount; ++n)
		{
			if(m_timbreMemoryFromRom[n])
				fill(Area::TimbreMemory, n, romMemoryKey(n));
		}
		return true;
	}

	std::vector<std::pair<uint32_t, uint32_t>> Memory::unknownRanges() const
	{
		std::vector<std::pair<uint32_t, uint32_t>> ranges;
		if(!m_hasLa)
			return ranges;
		for(const auto area : {Area::TimbreTemp, Area::TimbreMemory})
		{
			const auto& info = areaInfo(area);
			for(size_t e = 0; e < entryCount(area); ++e)
			{
				if(entryKnown(area, e))
					continue;
				// Entries in a row make one request, the gaps between the timbre memory's entries included
				const auto address = addressAdd(info.address, static_cast<uint32_t>(e * info.entryStride));
				const auto gap = info.entryStride - info.entrySize;
				if(!ranges.empty() && toLinear(ranges.back().first) + ranges.back().second + gap == toLinear(address))
					ranges.back().second += info.entryStride;
				else
					ranges.emplace_back(address, info.entrySize);
			}
		}
		return ranges;
	}

	std::vector<Message> Memory::createRestore() const
	{
		Memory fresh(m_model);
		fresh.m_presets = m_presets;
		fresh.reset();

		std::vector<Segment> segments;
		const auto differs = [&](const Area _area, const size_t _entry)
		{
			for(size_t i = 0; i < areaInfo(_area).entrySize; ++i)
			{
				if(!fresh.known(_area, _entry, i) || value(_area, _entry, i) != fresh.value(_area, _entry, i))
					return true;
			}
			return false;
		};
		const auto touchedAny = [&](const Area _area, const size_t _entry)
		{
			const auto& d = data(_area);
			const auto begin = d.touched.begin() + static_cast<ptrdiff_t>(index(_area, _entry, 0));
			return std::any_of(begin, begin + areaInfo(_area).entrySize, [](const uint8_t _t) { return _t != 0; });
		};
		// An entry known whole is written whole; of one that is not, the bytes written to it, which are known.
		// Each patch temp of the LA board in a message of its own: the MT-32 2.x loads only the timbre of the part
		// a message starts in.
		const auto addEntry = [&](const Area _area, const size_t _entry, const bool _whole)
		{
			const auto& info = areaInfo(_area);
			const auto base = addressAdd(info.address, static_cast<uint32_t>(_entry * info.entryStride));
			const auto* values = entry(_area, _entry);
			if(_whole)
			{
				addSegment(segments, base, values, info.entrySize, _area == Area::PatchTemp);
				return;
			}
			const auto& d = data(_area);
			const auto first = index(_area, _entry, 0);
			for(size_t i = 0; i < info.entrySize;)
			{
				if(!d.touched[first + i])
				{
					++i;
					continue;
				}
				size_t n = 0;
				while(i + n < info.entrySize && d.touched[first + i + n])
					++n;
				addSegment(segments, addressAdd(base, static_cast<uint32_t>(i)), values + i, n);
				i += n;
			}
		};
		const auto addArea = [&](const Area _area)
		{
			for(size_t e = 0; e < entryCount(_area); ++e)
			{
				if(entryKnown(_area, e))
				{
					if(differs(_area, e))
						addEntry(_area, e, true);
				}
				else if(touchedAny(_area, e))
				{
					addEntry(_area, e, false);
				}
			}
		};

		if(m_hasLa)
		{
			// The system first, whole, so that the old firmware takes the partial reserves, then the memories the
			// patch temps select from, then the patch temps, which load the parts' timbres again, then what was
			// written over those
			// Always: the reset keeps the master volume, which the board may have had otherwise before
			addEntry(Area::LaSystem, 0, true);
			addArea(Area::TimbreMemory);
			addArea(Area::PatchMemory);
			addArea(Area::RhythmSetup);
			for(size_t p = 0; p < g_laParts; ++p)
			{
				// A part may play another timbre than its patch temp selects, where the new firmware did not load
				// the timbre for a part after the first in a message. The part selects that timbre first, then the
				// selection goes over it in a message that starts in the part before, which loads nothing on the
				// MT-32 2.x; the CM-32L only leaves such a part with the group changed, and loads nothing for a group
				// alone either. Part 1 always comes first in a message, so never plays another timbre.
				const TimbreSource selection{value(Area::PatchTemp, p, 0), value(Area::PatchTemp, p, 1)};
				const auto base = addressAdd(g_patchTemp, static_cast<uint32_t>(p * g_patchTempSize));
				if(p > 0 && p < g_laTimbreParts && !(m_timbreSources[p] == selection))
				{
					std::array<uint8_t, g_patchTempSize> patch;
					std::memcpy(patch.data(), entry(Area::PatchTemp, p), g_patchTempSize);
					patch[0] = m_timbreSources[p].group;
					patch[1] = m_timbreSources[p].number;
					addSegment(segments, base, patch.data(), patch.size(), true);
					const uint8_t over[] = {value(Area::PatchTemp, p - 1, 0x0f), selection.group, selection.number};
					addSegment(segments, fromLinear(toLinear(base) - 1), over,
						selection.number == m_timbreSources[p].number ? 2 : 3, true);
				}
				else if(differs(Area::PatchTemp, p))
				{
					addEntry(Area::PatchTemp, p, true);
				}
			}
			for(size_t p = 0; p < g_laTimbreParts; ++p)
			{
				// A timbre from the memory is written whole: the memory may have changed since the part copied it.
				if(entryKnown(Area::TimbreTemp, p))
				{
					if(touchedAny(Area::TimbreTemp, p) || m_timbreSources[p].group == 2)
						addEntry(Area::TimbreTemp, p, true);
				}
				else if(touchedAny(Area::TimbreTemp, p))
				{
					addEntry(Area::TimbreTemp, p, false);
				}
			}
		}
		if(m_hasPcm)
		{
			addArea(Area::PcmSystem);
			// The first write to the patch memory has the board copy the factory patches to it, with the card
			// patches wrong; the patches go over that, and a patch memory still the ROM's needs no write at all
			// Only the bytes that differ from that copy: the misplaced card patches hold values out of range,
			// which a data set cannot write but would clamp
			if(m_pcmPatchMemoryInRam)
			{
				const auto& initialised = pcmInitialisedPatchMemory();
				const auto* values = entry(Area::PcmPatchMemory, 0);
				const auto before = segments.size();
				for(size_t i = 0; i < initialised.size();)
				{
					if(values[i] == initialised[i])
					{
						++i;
						continue;
					}
					size_t n = 0;
					while(i + n < initialised.size() && values[i + n] != initialised[i + n])
						++n;
					addSegment(segments, addressAdd(g_pcmPatchMemory, static_cast<uint32_t>(i)), values + i, n);
					i += n;
				}
				if(segments.size() == before)
					addSegment(segments, g_pcmPatchMemory, values, 1);
			}
			// A program change from one of those patches puts its values out of range in the patch temp too. No
			// data set can do that, so the restore writes what the board makes of the values it can write.
			for(size_t p = 0; p < g_pcmParts; ++p)
			{
				std::array<uint8_t, g_pcmPatchTempSize> patch;
				for(size_t i = 0; i < patch.size(); ++i)
				{
					patch[i] = value(Area::PcmPatchTemp, p, i);
					storeByte(Block::PcmPatchTemp, static_cast<uint8_t>(i), m_firmware, patch[i]);
				}
				if(!patch[0] && patch[1] >= g_pcmInternalTones)
					patch[1] = g_pcmNoTone;
				if(std::memcmp(patch.data(), fresh.entry(Area::PcmPatchTemp, p), patch.size()) != 0)
					addSegment(segments, addressAdd(g_pcmPatchTemp, static_cast<uint32_t>(p * g_pcmPatchTempSize)),
						patch.data(), patch.size());
			}
		}

		std::vector<Message> messages;
		messages.push_back(dt1(g_allParametersReset, std::vector<uint8_t>{0}));
		for(const auto& s : segments)
		{
			for(auto& m : dt1Chunks(s.address, s.data.data(), s.data.size()))
				messages.push_back(std::move(m));
		}
		return messages;
	}

	bool Memory::sameKnownState(const Memory& _other) const
	{
		if(_other.m_model != m_model)
			return false;
		for(size_t a = 0; a < m_areas.size(); ++a)
		{
			const auto& x = m_areas[a];
			const auto& y = _other.m_areas[a];
			for(size_t i = 0; i < x.values.size(); ++i)
			{
				if(x.known[i] && y.known[i] && x.values[i] != y.values[i])
					return false;
			}
		}
		return true;
	}
}
