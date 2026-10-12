// The GS message helpers and the part state the part editor shows. No ROMs.
#include "88lib/gs/gs.h"
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
		CHECK_EQ(tracker.setup().masterVolume, 90);

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
		tracker.process(sysex(0, dt1(partAddress(5, PartParam::UseForRhythm), 3)));
		CHECK_EQ(tracker.setup().parts[5].useForRhythm, 2);

		// GS Reset, GM System On: back to the defaults
		tracker.process(sysex(0, dt1(g_gsReset, 0x00)));
		s = tracker.setup();
		CHECK_EQ(s.parts[2].program, 0);
		CHECK_EQ(s.parts[0].rxChannel, 0);
		CHECK_EQ(s.masterVolume, 127);
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

int main()
{
	messages();
	defaults();
	channelMessages();
	sysexMessages();
	catalog();
	rules();
	return finish("partState");
}
