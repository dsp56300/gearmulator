#pragma once

#include "88lib/mt32/mt32.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace emu88Lib::mt32
{
	// Where a part's timbre temp came from: the timbre its patch temp selects, copied when the part selected it
	struct TimbreSource
	{
		uint8_t group = 0;	// 0 a, 1 b, 2 i (timbre memory), 3 r
		uint8_t number = 0;
		bool operator==(const TimbreSource& _s) const { return group == _s.group && number == _s.number; }
	};

	// A copy of the memory of a board speaking the MT-32 dialect, kept from the MIDI it receives: the LA board, the
	// CM-32P, or both on a CM-64. It follows the firmware's own rules - what it clamps, what a program change, a
	// controller or a patch write copies where - so the copy matches what a data request would read back, and it
	// can make the SysEx that set a freshly reset board to the same state (createRestore()), which is what a host
	// saves.
	//
	// Not every byte is known: the preset timbres (groups a, b and r) live in the LA board's ROM, so a part playing
	// one has a timbre temp that is only known once a data request read it - or once this copy saw that timbre whole
	// in an earlier answer, which it keeps. The old MT-32 firmware fills its timbre memory from a table in the ROM
	// too. An unknown byte is still reproducible: selecting the same timbre on the board loads the same data.
	//
	// What is not followed: writes outside the areas. The firmware does not check them all - a DT1 that runs past
	// the CM-32L's system area makes it echo data requests to its MIDI OUT instead of answering them.
	//
	// Not thread safe; StateTracker guards one for the player.
	class Memory
	{
	public:
		explicit Memory(DeviceModel _model = DeviceModel::Cm32l);

		DeviceModel model() const { return m_model; }
		LaFirmware firmware() const { return m_firmware; }
		bool hasLa() const { return m_hasLa; }
		bool hasPcm() const { return m_hasPcm; }

		// Power-on. The presets learned from answers stay.
		void reset();

		// A message to the board: true when anything here changed
		bool processSysex(const uint8_t* _data, size_t _size);
		bool processShort(uint8_t _status, uint8_t _data1, uint8_t _data2);
		// A data set the board sent in answer to a request: what it has there, without side effects
		bool processReply(const uint8_t* _data, size_t _size);

		// An area's bytes, entry by entry with the area's entry size (not its stride)
		uint8_t value(Area _area, size_t _entry, size_t _offset) const;
		bool known(Area _area, size_t _entry, size_t _offset) const;
		bool entryKnown(Area _area, size_t _entry) const;
		const uint8_t* entry(Area _area, size_t _entry) const;
		uint16_t entryCount(Area _area) const;
		TimbreSource timbreSource(size_t _part) const { return m_timbreSources[_part]; }

		// The receive channel of a part (0-8 on the LA board, 0-5 on the PCM board), as the system areas have it,
		// 16 = off
		uint8_t laChannel(size_t _part) const;
		uint8_t pcmChannel(size_t _part) const;
		// The channel an LA part does receive on. The same as laChannel() but on the old firmware after a reset:
		// that sets the system area back, while the parts keep their channels until the next write to one of them.
		uint8_t laReceiveChannel(size_t _part) const { return m_laReceiveChannels[_part]; }

		// What a data request has to read to know everything: (address, size) pairs, none on a CM-32P
		std::vector<std::pair<uint32_t, uint32_t>> unknownRanges() const;

		// SysEx that bring a board to this state: all parameters reset first, then the areas that differ from what
		// the reset leaves. A board needs a moment after the reset before it takes the rest, and the CM-32P another
		// after its first patch memory write, see g_restoreResetDelayMs. Not restored: a CM-32P patch temp value
		// out of range, which only a program change from the patches its firmware misplaces can set; the restore
		// has it as a data set leaves it.
		std::vector<Message> createRestore() const;

		// The same known bytes in every area
		bool sameKnownState(const Memory& _other) const;

		// A preset timbre's data, as an answer showed it; null when not seen
		const std::array<uint8_t, g_timbreSize>* preset(TimbreSource _source) const;
		// A preset timbre known from elsewhere than an answer, the control ROM say (see PresetCatalog): known from
		// now on as if an answer had shown it, over a reset too, and filled in where a part plays it
		void addPreset(TimbreSource _source, const std::array<uint8_t, g_timbreSize>& _data);

	private:
		struct AreaData
		{
			std::vector<uint8_t> values;
			std::vector<uint8_t> known;
			// Written by a message since the entry was last reset or loaded: for the bytes that are not known, what a
			// restore has to write
			std::vector<uint8_t> touched;
		};

		AreaData& data(Area _area) { return m_areas[static_cast<size_t>(_area)]; }
		const AreaData& data(Area _area) const { return m_areas[static_cast<size_t>(_area)]; }
		size_t index(Area _area, size_t _entry, size_t _offset) const;

		// All parameters reset, 7F xx xx: power-on but for the LA board's master volume, which stays
		void allParametersReset();
		void resetLa();
		void resetPcm();
		bool processDt1(const Dt1& _dt1);
		bool writeLa(uint32_t _address, const uint8_t* _data, size_t _size);
		bool writeChannelTimbre(uint8_t _channel, uint32_t _offset, const uint8_t* _data, size_t _size);
		bool writePcm(uint32_t _address, const uint8_t* _data, size_t _size);
		// One byte through the firmware's rules; false when nothing changed
		bool storeAt(const Location& _location, uint8_t _value);
		bool set(Area _area, size_t _entry, size_t _offset, uint8_t _value, bool _known = true);
		bool applyReserves(Area _area, const std::array<int, 9>& _written, size_t _count, uint8_t _total);
		// A part's timbre temp, copied again from what its patch temp selects
		void loadTimbre(size_t _part);
		void programChangeLa(size_t _part, uint8_t _program);
		void programChangePcm(size_t _part, uint8_t _program);
		bool checkPcmTone(size_t _part);
		void learnPreset(size_t _part);

		DeviceModel m_model;
		LaFirmware m_firmware;
		bool m_hasLa;
		bool m_hasPcm;
		std::array<AreaData, static_cast<size_t>(Area::Count)> m_areas;
		std::array<TimbreSource, g_laTimbreParts> m_timbreSources{};
		std::array<uint8_t, g_laParts> m_laReceiveChannels{};
		// The CM-32P's patch memory is the ROM's after power-on and a reset, until the first write copies it over
		bool m_pcmPatchMemoryInRam = false;
		// The old firmware's timbre memory, from the ROM after a reset until written
		std::array<bool, g_timbreMemoryCount> m_timbreMemoryFromRom{};

		// The RPN a data entry goes to, per part: a part that moves to another channel keeps its own. Neither board
		// takes an NRPN.
		struct RpnState
		{
			uint8_t msb = 0x7f;
			uint8_t lsb = 0x7f;
			bool nrpn = false;
		};
		// The LA board forgets it at a reset, the CM-32P keeps it
		std::array<RpnState, g_laParts> m_rpnLa{};
		std::array<RpnState, g_pcmParts> m_rpnPcm{};

		// Timbres from the ROM, as answers showed them: the presets by group and number, and the old firmware's
		// timbre memory as a reset leaves it
		std::map<uint16_t, std::array<uint8_t, g_timbreSize>> m_presets;
	};

	// How long the boards take over all parameters reset before they take the next message, in milliseconds
	constexpr uint32_t g_restoreResetDelayMs = 100;
}
