#pragma once

#include "88lib/deviceModel.h"
#include "88lib/gs/params.h"
#include "88lib/gs/toneCatalog.h"

#include "synthLib/midiTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace emu88Lib::gs
{
	// The boards the part editor knows: the Sound Canvases from the SC-55 to the SC-8850, and the boards that run
	// one of their programs.
	bool isPartEditorModel(DeviceModel _model);
	// The boards whose parts pick a tone map by bank select LSB (CC#32), 40 4x 00
	bool hasToneMaps(DeviceModel _model);
	// The boards with Rx Bank Select, 40 1x 23, which GM System On turns off on every part
	bool hasRxBankSelect(DeviceModel _model);
	// The boards with 40 1x 25, which keeps a part's tone when a program change selects nothing
	bool hasKeepTone(DeviceModel _model);
	// Whether the board answers data requests: the VE-GS Pro has no MIDI output of its own
	bool answersDataRequests(DeviceModel _model);

	struct Part
	{
		uint8_t bankMsb = 0;		// 40 1x 00, the variation
		uint8_t program = 0;		// 40 1x 01
		uint8_t rxChannel = 0;		// 40 1x 02, 0x10 = off
		uint8_t rxNoteMessage = 1;	// 40 1x 08, 0 = muted, as the front panel's MUTE does
		uint8_t useForRhythm = 0;	// 40 1x 15
		uint8_t level = 100;		// 40 1x 19, CC#7
		uint8_t pan = 64;			// 40 1x 1C, CC#10, 0 = random
		uint8_t rxBankSelect = 1;	// 40 1x 23
		uint8_t keepTone = 0;		// 40 1x 25
		uint8_t toneMap = 0;		// 40 4x 00, CC#32 at the program change
		uint8_t toneMap0 = 0;		// 40 4x 01

		// Bank select received and waiting for its program change, and the maps set by SysEx, which wait for it too
		uint8_t pendingBankMsb = 0;
		uint8_t pendingBankLsb = 0;
		uint8_t pendingToneMap0 = 0;

		// The parameters of partParams() without a field of their own, by their index there
		std::array<uint8_t, g_maxPartParams> values{};
		// The fine tune's LSB, 40 1x 2B, what RPN 0/1's data entry LSB sets
		uint8_t fineTuneLsb = 0;

		bool isDrumPart() const { return useForRhythm != 0; }

		// A parameter of partParams(), from its field where it has one
		uint16_t value(size_t _param) const;
		void setValue(size_t _param, uint16_t _value);

		bool operator==(const Part& _p) const;
		bool operator!=(const Part& _p) const { return !(*this == _p); }
	};

	// What a board's parts are set to: 16 parts per group, group A first, part 1 first within a group.
	struct Setup
	{
		std::vector<Part> parts;
		// The parameters of globalParams(), by their index there
		std::array<uint16_t, g_maxGlobalParams> globals{};

		uint16_t global(const char* _id) const;

		// The map a part plays from: its own, or the map-0 one when it is on map 0
		uint8_t effectiveToneMap(size_t _part) const;
	};

	// The state after a GS Reset: tone 0/0, part n on channel n of its group's port, part 10 of each group on drum
	// map 1, the board's own map as map 0, every other parameter at its default.
	Setup createDefaultSetup(DeviceModel _model);

	// Follows the MIDI a board receives and keeps a copy of what it sets on the parts and the effects. The player feeds
	// it everything it hands the board - live input, the song player, the part editor's own edits - and the board's
	// answers to data requests. A program change goes through the board's own rules, which the tone catalog describes:
	// one that the firmware ignores leaves the part as it was here too, and so does a value the board ignores. The
	// board's front panel changes nothing here, a data request catches up with it.
	//
	// process() runs under the processor's callback lock, from whatever thread holds it; the setup is guarded by
	// its own mutex so that the editor can copy it without blocking the audio callback for longer than that.
	class PartStateTracker
	{
	public:
		PartStateTracker();

		// Forgets everything and starts over with the board's defaults. Without a catalog a program change is
		// taken as it comes.
		void setModel(DeviceModel _model, std::shared_ptr<const ToneCatalog> _catalog = {});
		DeviceModel model() const;

		void reset();

		// A message to the board, on the port it arrives at
		void process(const synthLib::SMidiEvent& _event);
		// A data set from the board, in answer to a data request sent on _port: what the board has, as it is
		void processReply(const synthLib::SMidiEvent& _event, uint8_t _port);

		// Changes with every change of the setup
		uint64_t revision() const { return m_revision.load(std::memory_order_acquire); }
		Setup setup() const;
		Setup setup(uint64_t& _revision) const;

	private:
		void processLocked(const synthLib::SMidiEvent& _event, uint8_t _port, bool _reply);
		bool processChannelMessage(uint8_t _status, uint8_t _data1, uint8_t _data2, uint8_t _port);
		bool processSysex(const synthLib::SysexBuffer& _sysex, uint8_t _port, bool _reply);

		// A tone number set by SysEx, taken up once all of the message is in
		struct ToneWrite
		{
			Part* part = nullptr;
			int bankMsb = -1;
			int program = -1;
		};
		bool writeAddress(uint32_t _address, uint8_t _value, uint8_t _port, bool _reply, ToneWrite& _toneWrite);
		// The master tune and the fine tune take their bytes together: 0 when the address is no part of one, else its
		// size, with the byte's place in it
		uint8_t multiByteParam(uint32_t _address, uint8_t& _offset) const;
		bool writeMultiByte(uint32_t _address, const uint8_t* _data, uint8_t _port, bool _reply);
		// The group a part address is for, or a global one's port takes it: false when the board ignores it there
		bool targetGroup(uint8_t _high, uint8_t _port, uint8_t& _group) const;
		bool acceptsGlobal(uint8_t _high, uint8_t _mid, uint8_t _port) const;
		bool writeGlobal(size_t _param, uint16_t _value, bool _reply);
		// An insertion effect type as the board takes it: the parameters and sends to what the type sets
		bool selectEfxType(uint16_t _id, bool _reply);
		// A value for one of the twenty insertion effect parameters, as the current type has it
		bool acceptsEfxValue(size_t _index, uint16_t _value) const;
		bool writePart(Part& _part, size_t _param, uint16_t _value, bool _reply);
		// A part parameter of the table by its SysEx address, block nibble taken out
		bool writeTableParam(Part& _part, uint32_t _address, uint8_t _value, bool _reply);
		// The RPN and the NRPN that a data entry goes to, and which of the two was selected last. The SC-8850 keeps
		// them per channel, and a part that moves to another channel takes up that channel's; the others per part,
		// and forget them when the part's channel changes.
		struct RpnSelection
		{
			uint8_t rpnMsb = 0x7f;
			uint8_t rpnLsb = 0x7f;
			uint8_t nrpnMsb = 0x7f;
			uint8_t nrpnLsb = 0x7f;
			bool nrpnSelected = false;
			// The SC-8850's last data entry MSB in RPN mode, which a data entry LSB writes the fine tune with
			uint8_t fineTuneEntryMsb = 0;
		};
		void dataEntry(Part& _part, RpnSelection& _selection, uint8_t _value, bool _lsb);
		bool selectionPerChannel() const;
		// A program change, or a tone number by SysEx, as the firmware takes it
		void selectTone(Part& _part, uint8_t _bankMsb, uint8_t _program, bool _sysex) const;
		Part* partAt(uint8_t _group, uint8_t _block);
		enum class Reset : uint8_t { PowerOn, GsReset, GmSystemOn };
		void resetLocked(Reset _reset);
		void changed();

		mutable std::mutex m_mutex;
		DeviceModel m_model = DeviceModel::Sc88Pro;
		std::shared_ptr<const ToneCatalog> m_catalog;
		Setup m_setup;
		// Per part, or on the SC-8850 per port and channel, 16 per group either way
		std::vector<RpnSelection> m_selections;
		std::atomic<uint64_t> m_revision{1};
		// A GS Reset arrived since power-on: the SCC-1A's GM System On turns NRPN on from then on
		bool m_gsResetSeen = false;
	};
}
