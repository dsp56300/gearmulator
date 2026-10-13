// The GS message helpers and the part state the part editor shows. No ROMs.
#include "88lib/gs/efx.h"
#include "88lib/gs/gs.h"
#include "88lib/gs/midiClip.h"
#include "88lib/gs/params.h"
#include "88lib/gs/partState.h"
#include "88lib/gs/toneCatalog.h"
#include "cpu/common/test_util.hpp"

using namespace emu88Lib;
using namespace emu88Lib::gs;
using namespace test;

namespace
{
	synthLib::SMidiEvent channel(const uint8_t _port, const uint8_t _a, const uint8_t _b, const uint8_t _c = 0)
	{
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Host, _a, _b, _c);
		e.port = _port;
		return e;
	}

	synthLib::SMidiEvent sysex(const uint8_t _port, const Message& _message)
	{
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
		e.sysex.assign(_message.begin(), _message.end());
		e.port = _port;
		return e;
	}

	void messages()
	{
		// The GS Reset every manual prints
		CHECK(dt1(g_gsReset, 0x00) == Message({0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7}));
		// What the SC-8850 answered to this request in emulation
		CHECK(rq1(0x401119, 1) == Message({0xf0, 0x41, 0x10, 0x42, 0x11, 0x40, 0x11, 0x19, 0x00, 0x00, 0x01, 0x15, 0xf7}));

		CHECK_EQ(partBlock(9), 0);
		CHECK_EQ(partBlock(0), 1);
		CHECK_EQ(partBlock(15), 15);
		for(uint8_t p = 0; p < 16; ++p)
			CHECK_EQ(blockPart(partBlock(p)), p);
		CHECK_EQ(partAddress(0, PartParam::Level), 0x401119u);
		CHECK_EQ(partAddress(9, PartParam::ToneNumber), 0x401000u);
		CHECK_EQ(partAddress(10, PartMapParam::ToneMap), 0x404a00u);

		CHECK_EQ(addressAdd(0x40117f, 1), 0x401200u);
		CHECK_EQ(addressAdd(0x401119, 3), 0x40111cu);

		const auto m = dt1(0x401119, {0x64, 0x40});
		Dt1 parsed;
		CHECK(parseDt1(m.data(), m.size(), parsed));
		CHECK_EQ(parsed.address, 0x401119u);
		CHECK_EQ(parsed.size, 2u);
		auto broken = m;
		broken[broken.size() - 2] ^= 1;
		CHECK(!parseDt1(broken.data(), broken.size(), parsed));
	}

	void defaults()
	{
		CHECK(isPartEditorModel(DeviceModel::Sc55Mk1));
		CHECK(isPartEditorModel(DeviceModel::Sc8850));
		CHECK(!isPartEditorModel(DeviceModel::Cm32p));
		CHECK(!isPartEditorModel(DeviceModel::Mt32New));
		CHECK(!isPartEditorModel(DeviceModel::Nu10b));
		CHECK(!hasToneMaps(DeviceModel::Sc55Mk2));
		CHECK(hasToneMaps(DeviceModel::Sc88Pro));

		CHECK_EQ(createDefaultSetup(DeviceModel::Sc55Mk2).parts.size(), 16u);
		CHECK_EQ(createDefaultSetup(DeviceModel::Sc88Pro).parts.size(), 32u);
		CHECK_EQ(createDefaultSetup(DeviceModel::Sc8850).parts.size(), 64u);
		CHECK(createDefaultSetup(DeviceModel::Cm32p).parts.empty());

		const auto setup = createDefaultSetup(DeviceModel::Sc88Pro);
		CHECK(setup.parts[9].isDrumPart());
		CHECK(setup.parts[16 + 9].isDrumPart());
		CHECK(!setup.parts[0].isDrumPart());
		CHECK_EQ(setup.parts[16 + 4].rxChannel, 4);
		CHECK_EQ(setup.effectiveToneMap(0), 3);
	}

	void channelMessages()
	{
		PartStateTracker tracker;
		tracker.setModel(DeviceModel::Sc88Pro);
		auto revision = tracker.revision();

		// Bank select waits for its program change
		tracker.process(channel(0, 0xb2, 0, 8));
		tracker.process(channel(0, 0xb2, 32, 2));
		CHECK_EQ(tracker.setup().parts[2].bankMsb, 0);
		tracker.process(channel(0, 0xc2, 24));
		auto s = tracker.setup();
		CHECK_EQ(s.parts[2].bankMsb, 8);
		CHECK_EQ(s.parts[2].program, 24);
		CHECK_EQ(s.parts[2].toneMap, 2);
		CHECK_EQ(s.effectiveToneMap(2), 2);
		CHECK(tracker.revision() != revision);

		// Port B is group B
		tracker.process(channel(1, 0xb0, 7, 90));
		tracker.process(channel(1, 0xb0, 10, 20));
		s = tracker.setup();
		CHECK_EQ(s.parts[16].level, 90);
		CHECK_EQ(s.parts[16].pan, 20);
		CHECK_EQ(s.parts[0].level, 100);

		// A message that changes nothing leaves the revision alone
		revision = tracker.revision();
		tracker.process(channel(1, 0xb0, 7, 90));
		CHECK_EQ(tracker.revision(), revision);

		// Notes are no part settings
		tracker.process(channel(0, 0x90, 60, 100));
		CHECK_EQ(tracker.revision(), revision);
	}

	void sysexMessages()
	{
		PartStateTracker tracker;
		tracker.setModel(DeviceModel::Sc8850);

		// A part on channel 5 follows that channel only
		tracker.process(sysex(0, dt1(partAddress(0, PartParam::RxChannel), 4)));
		tracker.process(channel(0, 0xb4, 7, 33));
		auto s = tracker.setup();
		CHECK_EQ(s.parts[0].level, 33);
		CHECK_EQ(s.parts[4].level, 33);
		CHECK_EQ(s.parts[1].level, 100);

		// 40 on port C is group C; 50 on port A is group B, and the SC-8850 ignores it on port C
		tracker.process(sysex(2, dt1(partAddress(0, PartParam::Level), 5)));
		tracker.process(sysex(0, dt1(0x501119, 7)));
		tracker.process(sysex(2, dt1(0x501119, 9)));
		s = tracker.setup();
		CHECK_EQ(s.parts[32].level, 5);
		CHECK_EQ(s.parts[16].level, 7);
		CHECK_EQ(s.parts[48].level, 100);

		// A tone number in one message, and a write across several parameters
		tracker.process(sysex(0, dt1(partAddress(2, PartParam::ToneNumber), {16, 4})));
		tracker.process(sysex(0, dt1(partAddress(2, PartParam::Level), {80, 64, 64, 30})));
		s = tracker.setup();
		CHECK_EQ(s.parts[2].bankMsb, 16);
		CHECK_EQ(s.parts[2].program, 4);
		CHECK_EQ(s.parts[2].level, 80);
		CHECK_EQ(s.parts[2].pan, 30);

		// A map set by SysEx waits for the next program change
		tracker.process(sysex(0, dt1(partAddress(2, PartMapParam::ToneMap), 1)));
		CHECK_EQ(tracker.setup().effectiveToneMap(2), 4);
		tracker.process(channel(0, 0xc2, 4));
		CHECK_EQ(tracker.setup().effectiveToneMap(2), 1);
		tracker.process(sysex(0, dt1(g_masterVolume, 90)));
		CHECK_EQ(tracker.setup().global("MasterVolume"), 90);

		// A reply to a request on port B is about group B
		tracker.processReply(sysex(0, dt1(partAddress(0, PartParam::Pan), 1)), 1);
		CHECK_EQ(tracker.setup().parts[16].pan, 1);

		// The front panel's MUTE, and its SysEx
		tracker.process(sysex(0, dt1(partAddress(3, PartParam::RxNoteMessage), 0)));
		CHECK_EQ(tracker.setup().parts[3].rxNoteMessage, 0);

		// A part that turns into a drum part starts over on program 1
		tracker.process(sysex(0, dt1(partAddress(5, PartParam::ToneNumber), {8, 4})));
		tracker.process(sysex(0, dt1(partAddress(5, PartParam::UseForRhythm), 2)));
		s = tracker.setup();
		CHECK_EQ(s.parts[5].useForRhythm, 2);
		CHECK_EQ(s.parts[5].program, 0);
		CHECK_EQ(s.parts[5].bankMsb, 0);
		// The SC-8850 ignores a kind above 3, and takes 3 as drum map 1
		tracker.process(sysex(0, dt1(partAddress(5, PartParam::UseForRhythm), 4)));
		CHECK_EQ(tracker.setup().parts[5].useForRhythm, 2);
		tracker.process(sysex(0, dt1(partAddress(5, PartParam::UseForRhythm), 3)));
		CHECK_EQ(tracker.setup().parts[5].useForRhythm, 1);

		// GS Reset, GM System On: back to the defaults
		tracker.process(sysex(0, dt1(g_gsReset, 0x00)));
		s = tracker.setup();
		CHECK_EQ(s.parts[2].program, 0);
		CHECK_EQ(s.parts[0].rxChannel, 0);
		CHECK_EQ(s.global("MasterVolume"), 127);
		tracker.process(sysex(0, dt1(partAddress(3, PartParam::Level), 1)));
		tracker.process(sysex(0, {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7}));
		CHECK_EQ(tracker.setup().parts[3].level, 100);

		// The SC-55s have one map and ignore CC#32
		tracker.setModel(DeviceModel::Sc55Mk2);
		tracker.process(channel(0, 0xb0, 32, 2));
		tracker.process(channel(0, 0xc0, 1));
		CHECK_EQ(tracker.setup().parts[0].toneMap, 0);
		CHECK_EQ(tracker.setup().parts.size(), 16u);
		// Port B does not exist there
		const auto revision = tracker.revision();
		tracker.process(channel(1, 0xb0, 7, 1));
		CHECK_EQ(tracker.revision(), revision);
	}

	ToneMap testMap(const uint8_t _number)
	{
		ToneMap map;
		map.number = _number;
		map.tones = {{0, 0, "Piano 1"}, {8, 0, "Piano 1w"}, {0, 1, "Piano 2"}, {0, 2, "Piano 3"}};
		map.drumSets = {{0, "STANDARD 1"}, {8, "ROOM"}};
		map.userTones = _number != 1;
		return map;
	}

	void catalog()
	{
		ToneCatalog c;
		c.maps.push_back(testMap(1));
		c.maps.push_back(testMap(3));

		CHECK(c.findMap(3) == &c.maps[1]);
		CHECK(c.findMap(119) == nullptr);
		CHECK_EQ(c.selectTone(3, 8, 0, false).tone->name, std::string("Piano 1w"));
		CHECK_EQ(c.maps[0].variations(0).size(), 2u);
		CHECK(c.maps[0].find(8, 1) == nullptr);
		CHECK_EQ(c.selectDrumSet(3, 8).set->name, std::string("ROOM"));

		// User banks on every map but the SC-55 one
		CHECK(c.selectTone(3, 64, 5, false).selection == Selection::User);
		CHECK(c.selectTone(1, 64, 5, false).selection == Selection::Silent);
		CHECK(c.selectDrumSet(3, 65).selection == Selection::User);

		c.missingTone = MissingTone::KeepIfFlagged;
		CHECK(c.selectTone(3, 8, 1, true).selection == Selection::Ignored);
		CHECK(c.selectTone(3, 8, 1, false).selection == Selection::Silent);
		c.missingTone = MissingTone::KeepIfFlaggedBelow;
		CHECK(c.selectTone(3, 8, 1, true).selection == Selection::Ignored);
		CHECK(c.selectTone(3, 64, 1, true).selection == Selection::User);
		CHECK(c.selectTone(3, 0x48, 1, true).selection == Selection::Silent);
		CHECK(c.selectTone(3, 8, 0x78, true).selection == Selection::Silent);

		// The first SC-55 programs: bank & F8, then bank 0, below bank 64 and program 120
		c.missingTone = MissingTone::SubCapital;
		CHECK_EQ(c.selectTone(1, 9, 0, false).tone->name, std::string("Piano 1w"));
		CHECK_EQ(c.selectTone(1, 17, 1, false).tone->name, std::string("Piano 2"));
		CHECK(c.selectTone(1, 0x50, 1, false).selection == Selection::Silent);
		c.missingDrumSet = MissingDrumSet::SubCapital;
		c.drumLimit = 0x30;
		CHECK_EQ(c.selectDrumSet(1, 9).set->name, std::string("ROOM"));
		CHECK_EQ(c.selectDrumSet(1, 20).set->name, std::string("STANDARD 1"));
		CHECK(c.selectDrumSet(1, 0x30).selection == Selection::Silent);
	}

	// A program change goes through the firmware's rules
	void rules()
	{
		auto c = std::make_shared<ToneCatalog>();
		c->maps.push_back(testMap(1));
		c->maps.push_back(testMap(2));
		c->maps.push_back(testMap(3));
		c->missingTone = MissingTone::KeepIfFlagged;
		c->missingDrumSet = MissingDrumSet::Ignored;

		PartStateTracker tracker;
		tracker.setModel(DeviceModel::Sc88Pro, c);
		CHECK_EQ(tracker.setup().parts[0].keepTone, 1);

		// The SC-88Pro keeps the tone of a part a program change selects nothing for, and the map with it
		tracker.process(channel(0, 0xb0, 0, 8));
		tracker.process(channel(0, 0xc0, 0));
		tracker.process(channel(0, 0xb0, 0, 9));
		tracker.process(channel(0, 0xb0, 32, 2));
		tracker.process(channel(0, 0xc0, 0));
		auto s = tracker.setup();
		CHECK_EQ(s.parts[0].bankMsb, 8);
		CHECK_EQ(s.parts[0].program, 0);
		CHECK_EQ(s.parts[0].toneMap, 0);

		// With 40 1x 25 clear the part takes it and falls silent; the ignored one put the map latch back
		tracker.process(sysex(0, dt1(partAddress(0, PartParam::KeepTone), 0)));
		tracker.process(channel(0, 0xb0, 32, 2));
		tracker.process(channel(0, 0xc0, 0));
		s = tracker.setup();
		CHECK_EQ(s.parts[0].bankMsb, 9);
		CHECK_EQ(s.parts[0].toneMap, 2);

		// A tone number by SysEx goes the same way, and a reply is what the board has
		tracker.process(sysex(0, dt1(partAddress(1, PartParam::ToneNumber), {8, 1})));
		CHECK_EQ(tracker.setup().parts[1].program, 0);
		tracker.process(sysex(0, dt1(partAddress(1, PartParam::ToneNumber), {8, 0})));
		CHECK_EQ(tracker.setup().parts[1].bankMsb, 8);
		tracker.processReply(sysex(0, dt1(partAddress(1, PartParam::ToneNumber), {9, 1})), 0);
		CHECK_EQ(tracker.setup().parts[1].bankMsb, 9);
		CHECK_EQ(tracker.setup().parts[1].program, 1);

		// A drum set that is not there is ignored; one that is takes no variation
		tracker.process(channel(0, 0xb9, 0, 3));
		tracker.process(channel(0, 0xc9, 8));
		CHECK_EQ(tracker.setup().parts[9].program, 8);
		CHECK_EQ(tracker.setup().parts[9].bankMsb, 0);
		tracker.process(channel(0, 0xc9, 9));
		CHECK_EQ(tracker.setup().parts[9].program, 8);

		// GM System On turns bank select off on every part
		tracker.process(sysex(0, {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7}));
		tracker.process(channel(0, 0xb2, 0, 8));
		tracker.process(channel(0, 0xc2, 2));
		s = tracker.setup();
		CHECK_EQ(s.parts[2].rxBankSelect, 0);
		CHECK_EQ(s.parts[2].bankMsb, 0);
		CHECK_EQ(s.parts[2].program, 2);

		// The first SC-55 programs ignore a drum set selected with a bank, and know no Rx Bank Select
		auto c55 = std::make_shared<ToneCatalog>();
		c55->maps.push_back(testMap(0));
		c55->missingTone = MissingTone::SubCapital;
		c55->missingDrumSet = MissingDrumSet::SubCapital;
		c55->drumBankMustBeZero = true;
		tracker.setModel(DeviceModel::Sc55Mk1, c55);
		tracker.process(channel(0, 0xb9, 0, 1));
		tracker.process(channel(0, 0xc9, 8));
		CHECK_EQ(tracker.setup().parts[9].program, 0);
		tracker.process(sysex(0, {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7}));
		tracker.process(channel(0, 0xb0, 0, 9));
		tracker.process(channel(0, 0xc0, 0));
		CHECK_EQ(tracker.setup().parts[0].bankMsb, 9);
	}
}

namespace
{
	uint16_t partValue(const PartStateTracker& _tracker, const size_t _part, const char* _id)
	{
		return _tracker.setup().parts[_part].value(static_cast<size_t>(findPartParamById(_id)));
	}

	Message nrpn(const uint8_t _lsb, const uint8_t _value)
	{
		return {0xb0, 99, 1, 0xb0, 98, _lsb, 0xb0, 6, _value};
	}

	void controllers(PartStateTracker& _tracker, const uint8_t _port, const Message& _bytes)
	{
		for(size_t i = 0; i + 2 < _bytes.size(); i += 3)
			_tracker.process(channel(_port, _bytes[i], _bytes[i + 1], _bytes[i + 2]));
	}

	// What the emulated firmware made of these, read back with data requests
	void params()
	{
		CHECK(globalParams().size() <= g_maxGlobalParams);
		CHECK(partParams().size() <= g_maxPartParams);
		for(const auto* id : {"MasterVolume", "MasterTune", "Expression", "FineTune", "MonoPoly", "PartMode", "RxRpn",
			"RxNrpn"})
			CHECK(findGlobalParamById(id) >= 0 || findPartParamById(id) >= 0);
		CHECK_EQ(formatValue(partParams()[static_cast<size_t>(findPartParamById("KeyRangeLow"))], 60),
			std::string("C4"));
		CHECK_EQ(formatValue(globalParams()[static_cast<size_t>(findGlobalParamById("MasterTune"))], 1024),
			std::string("+0.0 ct"));

		PartStateTracker tracker;
		tracker.setModel(DeviceModel::Sc88Pro);
		// The SC-88Pro comes up with NRPN off, a GS Reset turns it on
		CHECK_EQ(partValue(tracker, 0, "RxNrpn"), 0);
		tracker.process(sysex(0, dt1(g_gsReset, 0x00)));
		CHECK_EQ(partValue(tracker, 0, "RxNrpn"), 1);
		auto s = tracker.setup();
		CHECK_EQ(s.global("ReverbMacro"), 4);
		CHECK_EQ(s.global("ChorusDepth"), 19);
		CHECK_EQ(partValue(tracker, 0, "ReverbSend"), 40);
		CHECK_EQ(partValue(tracker, 0, "Expression"), 127);

		// The controllers that set part parameters
		controllers(tracker, 0, {0xb0, 91, 11, 0xb0, 93, 12, 0xb0, 94, 13, 0xb0, 11, 90, 0xb0, 126, 1});
		CHECK_EQ(partValue(tracker, 0, "ReverbSend"), 11);
		CHECK_EQ(partValue(tracker, 0, "ChorusSend"), 12);
		CHECK_EQ(partValue(tracker, 0, "DelaySend"), 13);
		CHECK_EQ(partValue(tracker, 0, "Expression"), 90);
		CHECK_EQ(partValue(tracker, 0, "MonoPoly"), 0);
		controllers(tracker, 0, {0xb0, 127, 5, 0xb0, 121, 1});
		CHECK_EQ(partValue(tracker, 0, "MonoPoly"), 0);
		CHECK_EQ(partValue(tracker, 0, "Expression"), 90);
		controllers(tracker, 0, {0xb0, 127, 0, 0xb0, 121, 0, 0xb0, 126, 17});
		CHECK_EQ(partValue(tracker, 0, "MonoPoly"), 1);
		CHECK_EQ(partValue(tracker, 0, "Expression"), 127);

		// NRPN and RPN: a data entry goes to the kind selected last; Reset All Controllers selects neither
		controllers(tracker, 0, nrpn(0x20, 0x7f));
		CHECK_EQ(partValue(tracker, 0, "Cutoff"), 0x7f);
		controllers(tracker, 0, {0xb0, 101, 0, 0xb0, 100, 1, 0xb0, 6, 0x44, 0xb0, 38, 0x21});
		CHECK_EQ(partValue(tracker, 0, "FineTune"), 0x44);
		CHECK_EQ(tracker.setup().parts[0].fineTuneLsb, 0x21);
		CHECK_EQ(partValue(tracker, 0, "Cutoff"), 0x7f);
		controllers(tracker, 0, {0xb0, 6, 0x45});
		CHECK_EQ(tracker.setup().parts[0].fineTuneLsb, 0x21);
		controllers(tracker, 0, {0xb0, 99, 1, 0xb0, 6, 0x32});
		CHECK_EQ(partValue(tracker, 0, "Cutoff"), 0x32);
		controllers(tracker, 0, {0xb0, 121, 0, 0xb0, 6, 0x50});
		CHECK_EQ(partValue(tracker, 0, "Cutoff"), 0x32);
		controllers(tracker, 0, {0xb0, 101, 0, 0xb0, 100, 0, 0xb0, 6, 30});
		CHECK_EQ(partValue(tracker, 0, "BendRange"), 24);
		// The selection is the part's: one that moves to another channel has none
		controllers(tracker, 0, {0xb5, 99, 1, 0xb5, 98, 0x21});
		tracker.process(sysex(0, dt1(partAddress(3, PartParam::RxChannel), 5)));
		controllers(tracker, 0, {0xb5, 6, 0x30});
		CHECK_EQ(partValue(tracker, 3, "Resonance"), 64);
		CHECK_EQ(partValue(tracker, 5, "Resonance"), 0x30);

		// The fine tune takes both its bytes, or nothing
		tracker.process(sysex(0, dt1(0x40112a, 0x50)));
		CHECK_EQ(partValue(tracker, 0, "FineTune"), 0x45);
		tracker.process(sysex(0, dt1(0x40112a, {0x46, 0x22})));
		CHECK_EQ(partValue(tracker, 0, "FineTune"), 0x46);
		CHECK_EQ(tracker.setup().parts[0].fineTuneLsb, 0x22);

		// Out of range: clamped, a switch on, the insertion effect assign ignored
		tracker.process(sysex(0, dt1(0x401116, 0x7f)));
		tracker.process(sysex(0, dt1(0x401102, 0x7f)));
		tracker.process(sysex(0, dt1(0x401113, 0)));
		tracker.process(sysex(0, dt1(0x401113, 5)));
		tracker.process(sysex(0, dt1(0x404122, 5)));
		tracker.process(sysex(0, dt1(0x400150, 0x7f)));
		CHECK_EQ(partValue(tracker, 0, "KeyShift"), 88);
		CHECK_EQ(partValue(tracker, 0, "RxChannel"), 16);
		CHECK_EQ(partValue(tracker, 0, "MonoPoly"), 1);
		CHECK_EQ(partValue(tracker, 0, "EfxAssign"), 0);
		CHECK_EQ(tracker.setup().global("DelayMacro"), 9);
		tracker.process(sysex(0, dt1(partAddress(1, PartParam::UseForRhythm), 0x7f)));
		CHECK_EQ(tracker.setup().parts[1].useForRhythm, 2);

		// A macro sets its effect and clears its sends; what comes after it in the message stays
		tracker.process(sysex(0, dt1(0x400137, 30)));
		tracker.process(sysex(0, dt1(0x40013f, 30)));
		tracker.process(sysex(0, dt1(0x400130, {6, 6, 0, 64, 32, 50})));
		s = tracker.setup();
		CHECK_EQ(s.global("ReverbTime"), 32);
		CHECK_EQ(s.global("ReverbDelayFeedback"), 50);
		CHECK_EQ(s.global("ReverbPreDelay"), 0);
		CHECK_EQ(s.global("ChorusToReverb"), 30);
		tracker.process(sysex(0, dt1(0x400138, 5)));
		s = tracker.setup();
		CHECK_EQ(s.global("ChorusFeedback"), 112);
		CHECK_EQ(s.global("ChorusToReverb"), 0);

		// The master tune takes its four nibbles together, within 24 and 2024
		tracker.process(sysex(0, dt1(g_masterTune, {0, 0, 0, 0})));
		CHECK_EQ(tracker.setup().global("MasterTune"), 24);
		tracker.process(sysex(0, dt1(g_masterTune + 1, {5, 5, 5, 100})));
		s = tracker.setup();
		CHECK_EQ(s.global("MasterTune"), 24);
		CHECK_EQ(s.global("MasterVolume"), 100);

		// The SC-88 family takes the effects on port A only
		tracker.process(sysex(1, dt1(0x400133, 10)));
		CHECK_EQ(tracker.setup().global("ReverbLevel"), 64);

		// GM System On turns NRPN off
		tracker.process(sysex(0, {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7}));
		controllers(tracker, 0, nrpn(0x08, 0x50));
		CHECK_EQ(partValue(tracker, 0, "RxNrpn"), 0);
		CHECK_EQ(partValue(tracker, 0, "VibratoRate"), 64);

		// The SC-8850: NRPN on from power-on, effects from any port, values out of range ignored, a new coarse fine
		// tune drops the fine step
		tracker.setModel(DeviceModel::Sc8850);
		CHECK_EQ(partValue(tracker, 0, "RxNrpn"), 1);
		tracker.process(sysex(1, dt1(0x400133, 10)));
		CHECK_EQ(tracker.setup().global("ReverbLevel"), 10);
		tracker.process(sysex(0, dt1(0x500133, 11)));
		tracker.process(sysex(0, dt1(0x400150, 0x7f)));
		tracker.process(sysex(0, dt1(0x400152, 0x7f)));
		tracker.process(sysex(0, dt1(0x400131, 0x7f)));
		tracker.process(sysex(0, dt1(partAddress(1, PartParam::UseForRhythm), 0x7f)));
		s = tracker.setup();
		CHECK_EQ(s.global("ReverbLevel"), 11);
		CHECK_EQ(s.global("DelayMacro"), 0);
		CHECK_EQ(s.global("DelayTimeCenter"), 97);
		CHECK_EQ(s.global("ReverbCharacter"), 4);
		CHECK_EQ(s.parts[1].useForRhythm, 0);
		controllers(tracker, 0, {0xb0, 101, 0, 0xb0, 100, 1, 0xb0, 6, 0x44, 0xb0, 38, 0x21, 0xb0, 6, 0x45});
		CHECK_EQ(tracker.setup().parts[0].fineTuneLsb, 0);
		// ... and a data entry LSB brings the last coarse step back, through a reset and with the selection it keeps
		tracker.process(sysex(0, dt1(0x40112a, {0x41, 0x26})));
		tracker.process(sysex(0, dt1(g_gsReset, 0x00)));
		controllers(tracker, 0, {0xb0, 38, 0x27});
		CHECK_EQ(partValue(tracker, 0, "FineTune"), 0x45);
		CHECK_EQ(tracker.setup().parts[0].fineTuneLsb, 0x27);
		// The selection is the channel's: a part that moves there takes it up
		controllers(tracker, 0, {0xb5, 99, 1, 0xb5, 98, 0x21});
		tracker.process(sysex(0, dt1(partAddress(3, PartParam::RxChannel), 5)));
		controllers(tracker, 0, {0xb5, 6, 0x31});
		CHECK_EQ(partValue(tracker, 3, "Resonance"), 0x31);
		CHECK_EQ(partValue(tracker, 5, "Resonance"), 0x31);
		// An NRPN MSB of 7F leaves the RPN selected
		controllers(tracker, 0, {0xb0, 101, 0, 0xb0, 100, 1, 0xb0, 99, 0x7f, 0xb0, 6, 0x20});
		CHECK_EQ(partValue(tracker, 0, "FineTune"), 0x20);
		// The chorus send to delay only from a message that starts there; part mode 3 is drum map 1
		tracker.process(sysex(0, dt1(0x40013f, {0x15, 0x16})));
		tracker.process(sysex(0, dt1(partAddress(2, PartParam::UseForRhythm), 3)));
		s = tracker.setup();
		CHECK_EQ(s.global("ChorusToReverb"), 0x15);
		CHECK_EQ(s.global("ChorusToDelay"), 0);
		CHECK_EQ(s.parts[2].useForRhythm, 1);

		// The SC-55mkII keeps the tone modify within the GS range; the first SC-55 programs the cutoff below its
		// centre, keep NRPN on after GM System On and do not know the universal master volume
		tracker.setModel(DeviceModel::Sc55Mk2);
		tracker.process(sysex(0, dt1(g_gsReset, 0x00)));
		controllers(tracker, 0, nrpn(0x20, 0x7f));
		controllers(tracker, 0, {0xb0, 94, 50});
		CHECK_EQ(partValue(tracker, 0, "Cutoff"), 0x72);
		CHECK_EQ(partValue(tracker, 0, "DelaySend"), 0);
		tracker.setModel(DeviceModel::Sc55Mk1);
		controllers(tracker, 0, nrpn(0x20, 0x7f));
		CHECK_EQ(partValue(tracker, 0, "Cutoff"), 0x50);
		tracker.process(sysex(0, {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7}));
		controllers(tracker, 0, nrpn(0x08, 0x00));
		CHECK_EQ(partValue(tracker, 0, "VibratoRate"), 0x0e);
		tracker.process(sysex(0, {0xf0, 0x7f, 0x7f, 0x04, 0x01, 0x00, 0x33, 0xf7}));
		CHECK_EQ(tracker.setup().global("MasterVolume"), 127);
		tracker.process(sysex(1, dt1(0x400133, 10)));
		CHECK_EQ(tracker.setup().global("ReverbLevel"), 10);
	}
}

namespace
{
	// What the emulated SC-88Pro and SC-8850 did with the insertion effect
	void insertionEffect()
	{
		PartStateTracker tracker;
		tracker.setModel(DeviceModel::Sc88Pro);
		auto s = tracker.setup();
		CHECK_EQ(s.global("EfxType"), 0);
		CHECK_EQ(s.global("EfxParam20"), 127);
		CHECK_EQ(s.global("EfxToReverb"), 40);
		CHECK_EQ(s.global("EfxDepth1"), 64);
		CHECK_EQ(s.global("EfxToEq"), 1);
		CHECK_EQ(std::string(findEfxType(0x0111).name), std::string("Distortion"));
		CHECK_EQ(findEfxType(0x0105).id, 0);

		// A type takes both bytes, and sets its parameters and the sends
		tracker.process(sysex(0, dt1(0x400317, 99)));
		tracker.process(sysex(0, dt1(0x40031b, 7)));
		tracker.process(sysex(0, dt1(g_efxTypeAddress, 0x01)));
		CHECK_EQ(tracker.setup().global("EfxType"), 0);
		tracker.process(sysex(0, dt1(g_efxTypeAddress, {0x01, 0x11})));
		s = tracker.setup();
		CHECK_EQ(s.global("EfxType"), 0x0111);
		CHECK_EQ(s.global("EfxParam1"), 76);
		CHECK_EQ(s.global("EfxParam2"), 3);
		CHECK_EQ(s.global("EfxParam18"), 56);
		CHECK_EQ(s.global("EfxParam20"), 84);
		CHECK_EQ(s.global("EfxToReverb"), 40);
		CHECK_EQ(s.global("EfxSource1"), 7);

		// The SC-88Pro: a checked parameter ignores a value out of its range, an unchecked one takes it, one the type
		// does not have takes nothing; any port, and 50 from port A
		tracker.process(sysex(0, dt1(0x400304, 33)));
		tracker.process(sysex(1, dt1(0x400303, 33)));
		tracker.process(sysex(0, dt1(0x400308, 33)));
		tracker.process(sysex(0, dt1(0x500313, 0)));
		tracker.process(sysex(0, dt1(0x40031b, 96)));
		s = tracker.setup();
		CHECK_EQ(s.global("EfxParam2"), 3);
		CHECK_EQ(s.global("EfxParam1"), 33);
		CHECK_EQ(s.global("EfxParam6"), 0);
		CHECK_EQ(s.global("EfxParam17"), 0);
		CHECK_EQ(s.global("EfxSource1"), 7);

		// The same type again starts the parameters over; an unknown one is Thru
		tracker.process(sysex(0, dt1(g_efxTypeAddress, {0x01, 0x11})));
		CHECK_EQ(tracker.setup().global("EfxParam1"), 76);
		tracker.process(sysex(0, dt1(g_efxTypeAddress, {0x01, 0x05})));
		CHECK_EQ(tracker.setup().global("EfxType"), 0);

		// A reply is what the board has
		tracker.processReply(sysex(0, dt1(g_efxTypeAddress, {0x01, 0x20})), 0);
		s = tracker.setup();
		CHECK_EQ(s.global("EfxType"), 0x0120);
		CHECK_EQ(s.global("EfxParam20"), 127);

		// The SC-8850 checks nothing, and drops a message that starts at the type's LSB
		tracker.setModel(DeviceModel::Sc8850);
		tracker.process(sysex(0, dt1(g_efxTypeAddress, {0x01, 0x11})));
		tracker.process(sysex(0, dt1(0x400304, 33)));
		tracker.process(sysex(0, dt1(0x400308, 33)));
		tracker.process(sysex(0, dt1(0x40031b, 127)));
		tracker.process(sysex(0, dt1(0x400301, {0x20, 0x00, 0x31})));
		s = tracker.setup();
		CHECK_EQ(s.global("EfxParam2"), 33);
		CHECK_EQ(s.global("EfxParam6"), 33);
		CHECK_EQ(s.global("EfxSource1"), 127);
		CHECK_EQ(s.global("EfxType"), 0x0111);
		CHECK_EQ(s.global("EfxParam1"), 76);

		// GS Reset: Thru
		tracker.process(sysex(0, dt1(g_gsReset, 0x00)));
		CHECK_EQ(tracker.setup().global("EfxType"), 0);
	}
}

namespace
{
	uint32_t readBigEndian(const std::vector<uint8_t>& _data, const size_t _offset, const size_t _bytes)
	{
		uint32_t v = 0;
		for(size_t i = 0; i < _bytes; ++i)
			v = v << 8 | _data[_offset + i];
		return v;
	}

	// A setup as a Standard MIDI File: format 1 with one track, which Logic does not split by channel
	void midiClip()
	{
		MidiClip clip;
		clip.add(dt1(g_gsReset, 0x00));
		clip.add(dt1(partAddress(0, PartParam::Level), 90));
		clip.add({0xb0, 11, 100});
		clip.add({0xc0, 5});
		CHECK(isResetMessage(dt1(g_gsReset, 0x00)));
		CHECK(!isResetMessage(dt1(partAddress(0, PartParam::Level), 90)));
		CHECK_EQ(clip.events()[1].time, MidiClip::g_resetSpacing);
		CHECK_EQ(clip.events()[2].time, MidiClip::g_resetSpacing + MidiClip::g_sysexSpacing);

		const auto smf = clip.toSmf("Setup");
		CHECK_EQ(readBigEndian(smf, 0, 4), 0x4d546864u);	// MThd
		CHECK_EQ(readBigEndian(smf, 8, 2), 1u);
		CHECK_EQ(readBigEndian(smf, 10, 2), 1u);
		CHECK_EQ(readBigEndian(smf, 12, 2), 480u);
		CHECK_EQ(readBigEndian(smf, 14, 4), 0x4d54726bu);	// MTrk
		CHECK_EQ(readBigEndian(smf, 18, 4) + 22, smf.size());
		// The name, then the reset at tick 0 as F0 <length of the rest>
		CHECK_EQ(smf[22], 0x00);
		CHECK_EQ(smf[23], 0xff);
		CHECK_EQ(smf[24], 0x03);
		CHECK_EQ(smf[25], 5);
		CHECK_EQ(smf[31], 0x00);
		CHECK_EQ(smf[32], 0xf0);
		CHECK_EQ(smf[33], 10);
		// The program change with its one data byte, then the end of the track on the bar line
		CHECK_EQ(smf[smf.size() - 7], 0xc0);
		CHECK_EQ(smf[smf.size() - 6], 5);
		CHECK_EQ(smf[smf.size() - 3], 0xff);
		CHECK_EQ(smf[smf.size() - 2], 0x2f);
	}

	// Which parts the channel messages go to, for the part tabs to light up
	void activity()
	{
		PartStateTracker tracker;
		tracker.setModel(DeviceModel::Sc88Pro);

		// Nothing is kept while nothing shows it
		tracker.process(channel(0, 0x90, 60, 100));
		CHECK_EQ(tracker.takeActivity(), 0u);
		tracker.setActivityEnabled(true);

		// A note on channel 1 of port B reaches part B1; once taken it is gone
		tracker.process(channel(1, 0x90, 60, 100));
		CHECK_EQ(tracker.takeActivity(), uint64_t{1} << 16);
		CHECK_EQ(tracker.takeActivity(), 0u);

		// Every part on the channel, whatever the message
		tracker.process(sysex(0, dt1(partAddress(3, PartParam::RxChannel), 0)));
		tracker.process(channel(0, 0xe0, 0, 0x40));
		CHECK_EQ(tracker.takeActivity(), (uint64_t{1} << 0) | (uint64_t{1} << 3));

		// A part switched off hears nothing, and a channel without parts lights none
		tracker.process(sysex(0, dt1(partAddress(0, PartParam::RxChannel), 0x10)));
		tracker.process(sysex(0, dt1(partAddress(3, PartParam::RxChannel), 0x10)));
		tracker.process(channel(0, 0x90, 60, 100));
		tracker.process(channel(0, 0xf8, 0));
		CHECK_EQ(tracker.takeActivity(), 0u);

		// The SC-8850's port D
		tracker.setModel(DeviceModel::Sc8850);
		tracker.process(channel(3, 0x99, 36, 100));
		CHECK_EQ(tracker.takeActivity(), uint64_t{1} << (48 + 9));

		// Switched off, what came in is gone and nothing more is kept
		tracker.process(channel(0, 0x90, 60, 100));
		tracker.setActivityEnabled(false);
		tracker.process(channel(0, 0x90, 60, 100));
		CHECK_EQ(tracker.takeActivity(), 0u);
	}
}

int main()
{
	messages();
	defaults();
	channelMessages();
	sysexMessages();
	catalog();
	rules();
	params();
	insertionEffect();
	midiClip();
	activity();
	return finish("partState");
}
