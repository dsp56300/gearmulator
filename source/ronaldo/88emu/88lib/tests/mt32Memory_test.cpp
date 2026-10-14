// The MT-32 dialect's memory copy, without the boards: the rules it carries were read back from the emulated
// firmware, mt32Firmware_test checks them against it.
#include "88lib/mt32/memory.h"
#include "88lib/mt32/params.h"
#include "88lib/mt32/stateTracker.h"
#include "cpu/common/test_util.hpp"

#include <random>

using namespace emu88Lib;
using namespace emu88Lib::mt32;

namespace
{
	bool send(Memory& _m, const Message& _msg) { return _m.processSysex(_msg.data(), _msg.size()); }
	bool send(Memory& _m, const uint32_t _address, const std::vector<uint8_t>& _data, const uint8_t _dev = 0x10)
	{
		return send(_m, dt1(_address, _data, _dev));
	}
	uint8_t pt(const Memory& _m, const size_t _part, const size_t _offset)
	{
		return _m.value(Area::PatchTemp, _part, _offset);
	}

	void testMessages()
	{
		// The manual's examples
		const Message request{0xf0, 0x41, 0x10, 0x16, 0x11, 0x04, 0x01, 0x76, 0x00, 0x01, 0x76, 0x0e, 0xf7};
		CHECK(rq1(0x040176, 0xf6) == request);
		const Message reserves{0xf0, 0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x04, 0x08, 0x0a, 0, 0, 0, 0, 0, 0, 0x08,
			0x52, 0xf7};
		CHECK(dt1(0x100004, {8, 10, 0, 0, 0, 0, 0, 0, 8}) == reserves);
		Dt1 parsed;
		CHECK(parseDt1(reserves.data(), reserves.size(), parsed));
		CHECK_EQ(parsed.address, 0x100004u);
		CHECK_EQ(parsed.size, size_t(9));
		auto broken = reserves;
		broken[8] ^= 1;
		CHECK(!parseDt1(broken.data(), broken.size(), parsed));

		std::vector<uint8_t> big(600, 1);
		const auto chunks = dt1Chunks(g_timbreMemory, big.data(), big.size());
		CHECK_EQ(chunks.size(), size_t(3));
		CHECK(parseDt1(chunks[2].data(), chunks[2].size(), parsed));
		CHECK_EQ(parsed.address, 0x080400u);
		CHECK_EQ(parsed.size, size_t(88));

		// The address map
		auto loc = locate(Board::La, 0x030110, LaFirmware::New);
		CHECK(loc.area == Area::RhythmSetup && loc.entry == 0 && loc.offset == 0);
		loc = locate(Board::La, 0x030100, LaFirmware::New);
		CHECK(loc.area == Area::PatchTemp && loc.entry == 8);
		loc = locate(Board::La, 0x040176, LaFirmware::New);
		CHECK(loc.area == Area::TimbreTemp && loc.entry == 1 && loc.offset == 0);
		CHECK(!locate(Board::La, 0x080176, LaFirmware::New).valid());	// the gap behind a timbre memory entry
		loc = locate(Board::La, 0x080200, LaFirmware::New);
		CHECK(loc.area == Area::TimbreMemory && loc.entry == 1);
		CHECK(locate(Board::La, 0x03030c, LaFirmware::Old).valid());	// key 87
		CHECK(!locate(Board::La, 0x030310, LaFirmware::Old).valid());	// key 88, new firmware only
		CHECK(locate(Board::La, 0x030310, LaFirmware::New).valid());
		CHECK(!locate(Board::La, 0x500000, LaFirmware::New).valid());
		loc = locate(Board::Pcm, 0x51126d, LaFirmware::New);
		CHECK(loc.area == Area::PcmPatchMemory && loc.entry == 127);
		CHECK_EQ(address({Area::TimbreTemp, 7, 0}), 0x040d3au);
	}

	void testParams()
	{
		uint8_t v = 0x7f;
		CHECK(storeByte(Block::PatchTemp, 0x02, LaFirmware::New, v) && v == 48);
		v = 0x7f;
		CHECK(storeByte(Block::PatchTemp, 0x05, LaFirmware::New, v) && v == 3);
		v = 0x7f;
		CHECK(storeByte(Block::PatchTemp, 0x05, LaFirmware::Old, v) && v == 4);
		v = 0x55;
		CHECK(storeByte(Block::PatchTemp, 0x0b, LaFirmware::New, v) && v == 0x55);
		CHECK(!storeByte(Block::PatchTemp, 0x0b, LaFirmware::Old, v));
		CHECK(!storeByte(Block::PatchTemp, 0x0c, LaFirmware::New, v));
		v = 0x7f;
		CHECK(storeByte(Block::Partial, 0x09, LaFirmware::New, v) && v == 3);
		v = 0x7f;
		CHECK(storeByte(Block::Partial, 0x09, LaFirmware::Old, v) && v == 100);
		v = 0x7f;
		CHECK(storeByte(Block::TimbreCommon, 0x03, LaFirmware::New, v) && v == 0x7f);	// a name byte

		const auto& partial = params(Block::Partial);
		CHECK_EQ(partial.size(), g_partialSize);
		const auto& pan = params(Block::PatchTemp)[findParamById(Block::PatchTemp, "Pan")];
		CHECK_EQ(formatValue(pan, 0), std::string("R7"));
		CHECK_EQ(formatValue(pan, 7), std::string("C"));
		CHECK_EQ(formatValue(pan, 14), std::string("L7"));
		const auto& coarse = partial[findParamById(Block::Partial, "PitchCoarse")];
		CHECK_EQ(formatValue(coarse, 0), std::string("C1"));
		CHECK_EQ(formatValue(coarse, 96), std::string("C9"));
		const auto& bias = partial[findParamById(Block::Partial, "TvfBiasPoint")];
		CHECK_EQ(formatValue(bias, 0), std::string("<A1"));
		CHECK_EQ(formatValue(bias, 127), std::string(">C7"));
		const auto& keyf = partial[findParamById(Block::Partial, "PitchKeyfollow")];
		CHECK_EQ(valueList(keyf).size(), size_t(17));
		const auto& system = params(Block::LaSystem);
		CHECK_EQ(formatValue(system[0], 64), std::string("440.0 Hz"));
		CHECK_EQ(formatValue(system[0], 0), std::string("427.5 Hz"));
		CHECK_EQ(formatValue(system[0], 127), std::string("452.7 Hz"));
		const auto& pcmSystem = params(Block::PcmSystem);
		CHECK_EQ(formatValue(pcmSystem[0], 64), std::string("442.0 Hz"));
		CHECK_EQ(formatValue(pcmSystem[0], 0), std::string("426.0 Hz"));
		CHECK_EQ(formatValue(pcmSystem[0], 127), std::string("458.4 Hz"));
		CHECK_EQ(formatValue(params(Block::RhythmKey)[0], 0x40), std::string("r01"));
	}

	void testDefaults()
	{
		const Memory cm(DeviceModel::Cm32l);
		CHECK(cm.hasLa() && !cm.hasPcm());
		CHECK_EQ(cm.value(Area::LaSystem, 0, 0), uint8_t(0x4a));
		CHECK_EQ(cm.laChannel(0), uint8_t(1));
		CHECK_EQ(cm.laChannel(8), uint8_t(9));
		CHECK_EQ(pt(cm, 0, 1), uint8_t(4));
		CHECK_EQ(pt(cm, 0, 0x0b), uint8_t(0x7f));
		CHECK_EQ(cm.entryCount(Area::RhythmSetup), uint16_t(85));
		CHECK_EQ(cm.value(Area::RhythmSetup, 0, 1), uint8_t(0));		// empty CM-32L keys are silent
		CHECK_EQ(cm.value(Area::RhythmSetup, 84, 0), uint8_t(0x7e));
		CHECK(cm.entryKnown(Area::TimbreMemory, 0));
		CHECK(!cm.entryKnown(Area::TimbreTemp, 0));
		CHECK(cm.timbreSource(0) == (TimbreSource{1, 4}));
		CHECK_EQ(cm.unknownRanges().size(), size_t(1));
		CHECK_EQ(cm.unknownRanges()[0].second, uint32_t(8 * g_timbreSize));

		const Memory old(DeviceModel::Mt32Old);
		CHECK_EQ(pt(old, 0, 0x0b), uint8_t(0));
		CHECK_EQ(old.entryCount(Area::RhythmSetup), uint16_t(64));
		CHECK_EQ(old.value(Area::RhythmSetup, 0, 1), uint8_t(100));
		CHECK(!old.entryKnown(Area::TimbreMemory, 0));
		// One request for the timbre temps, one for the timbre memory, gaps and all: the board answers across them
		CHECK_EQ(old.unknownRanges().size(), size_t(2));

		const Memory pcm(DeviceModel::Cm32p);
		CHECK(!pcm.hasLa() && pcm.hasPcm());
		CHECK_EQ(pcm.pcmChannel(0), uint8_t(10));
		CHECK_EQ(pcm.value(Area::PcmPatchTemp, 0, 1), uint8_t(0x28));
		CHECK_EQ(pcm.value(Area::PcmPatchMemory, 5, 1), uint8_t(6));
		CHECK_EQ(pcm.value(Area::PcmPatchMemory, 64, 0), uint8_t(1));
		CHECK(pcm.unknownRanges().empty());
	}

	void testLaRules(const DeviceModel _model)
	{
		Memory m(_model);
		const bool oldFw = m.firmware() == LaFirmware::Old;

		// Program change: patch memory over the patch temp, level and pan kept, timbre loaded again
		send(m, g_timbreMemory, {'P', 'R', 'O', 'B', 'E'});
		send(m, 0x050008, {2, 0, 30, 50, 12, 1, 0});
		send(m, 0x030008, {90, 3});
		m.processShort(0xc1, 1, 0);
		CHECK_EQ(pt(m, 0, 0), uint8_t(2));
		CHECK_EQ(pt(m, 0, 2), uint8_t(30));
		CHECK_EQ(pt(m, 0, 8), uint8_t(90));
		CHECK_EQ(pt(m, 0, 9), uint8_t(3));
		// The old firmware's timbre memory is a copy of group a, known only where it was written
		CHECK_EQ(m.entryKnown(Area::TimbreTemp, 0), !oldFw);
		CHECK(m.known(Area::TimbreTemp, 0, 0));
		CHECK_EQ(m.value(Area::TimbreTemp, 0, 0), uint8_t('P'));
		// Memory written after the part copied it: the part keeps its copy
		send(m, g_timbreMemory, {'Q'});
		CHECK_EQ(m.value(Area::TimbreTemp, 0, 0), uint8_t('P'));
		// The rhythm part ignores program changes
		m.processShort(0xc9, 5, 0);
		CHECK_EQ(pt(m, 8, 1), uint8_t(0));

		// Controllers write the patch temp
		m.processShort(0xb1, 7, 64);
		CHECK_EQ(pt(m, 0, 8), uint8_t(50));
		m.processShort(0xb1, 10, 17);
		CHECK_EQ(pt(m, 0, 9), uint8_t(oldFw ? 1 : 2));
		m.processShort(0xb1, 10, 127);
		CHECK_EQ(pt(m, 0, 9), uint8_t(14));
		m.processShort(0xb1, 6, 5);		// no RPN selected yet
		CHECK_EQ(pt(m, 0, 4), uint8_t(12));
		m.processShort(0xb1, 101, 0);
		m.processShort(0xb1, 100, 0);
		m.processShort(0xb1, 6, 40);
		CHECK_EQ(pt(m, 0, 4), uint8_t(oldFw ? 12 : 24));
		m.processShort(0xb1, 99, 0);
		m.processShort(0xb1, 6, 3);		// NRPN selected now
		CHECK_EQ(pt(m, 0, 4), uint8_t(oldFw ? 12 : 24));
		// The selection is the part's: it goes with the part to another channel
		m.processShort(0xb1, 100, 0);
		send(m, 0x10000d, {4});
		m.processShort(0xb4, 6, 7);
		CHECK_EQ(pt(m, 0, 4), uint8_t(oldFw ? 12 : 7));
		m.processShort(0xb3, 101, 0);
		m.processShort(0xb3, 100, 0);
		send(m, 0x10000d, {3});
		m.processShort(0xb3, 6, 9);
		CHECK_EQ(pt(m, 0, 4), uint8_t(oldFw ? 12 : 9));
		send(m, 0x10000d, {1});

		// Clamping, and the parts reloading their timbre
		send(m, 0x030010, {0, 0x50});
		CHECK_EQ(pt(m, 1, 1), uint8_t(63));
		CHECK(m.timbreSource(1) == (TimbreSource{0, 63}));
		send(m, 0x020000, {'X'}, 0x01);		// part 1 is on channel 2
		CHECK_EQ(m.value(Area::TimbreTemp, 0, 0), uint8_t('X'));
		send(m, 0x030002, {30});
		// The old firmware takes the timbre again at any patch temp write, from the memory as it is now
		CHECK_EQ(m.value(Area::TimbreTemp, 0, 0), uint8_t(oldFw ? 'Q' : 'X'));

		// The channel addressed timbre: the old firmware runs on into the next part
		send(m, 0x020176, {'Y'}, 0x01);
		CHECK_EQ(m.known(Area::TimbreTemp, 1, 0), oldFw);

		// Partial reserves
		send(m, 0x100006, {20});
		CHECK_EQ(m.value(Area::LaSystem, 0, 6), uint8_t(oldFw ? 6 : 19));
		CHECK_EQ(m.value(Area::LaSystem, 0, 12), uint8_t(oldFw ? 6 : 0));
		send(m, 0x100004, {10, 10, 10, 10, 10, 10, 10, 10, 10});
		CHECK_EQ(m.value(Area::LaSystem, 0, 7), uint8_t(2));
		CHECK_EQ(m.value(Area::LaSystem, 0, 8), uint8_t(0));

		// Moving a part to another channel
		send(m, 0x10000d, {0x7f});
		CHECK_EQ(m.laChannel(0), uint8_t(16));
		send(m, 0x10000d, {5});
		m.processShort(0xc5, 3, 0);
		CHECK_EQ(pt(m, 0, 1), uint8_t(3));

		send(m, g_allParametersReset, {0});
		CHECK(m.sameKnownState(Memory(_model)));
	}

	// The quirks of single firmware versions, as the emulated boards showed them
	void testFirmwareQuirks()
	{
		{
			// The old firmware: a system write starting among the partial reserves skips them and puts its bytes
			// after them from its first one on
			Memory m(DeviceModel::Mt32Old);
			send(m, 0x10000c, {1, 15, 11, 1});
			CHECK_EQ(m.value(Area::LaSystem, 0, 0x0c), uint8_t(6));
			CHECK_EQ(m.laChannel(0), uint8_t(1));
			CHECK_EQ(m.laChannel(1), uint8_t(15));
			CHECK_EQ(m.laChannel(2), uint8_t(11));
			CHECK_EQ(m.laChannel(3), uint8_t(4));
			// Its cut of the reserves at 32 clears part 1's channel too
			send(m, 0x100004, {10, 10, 10, 10, 10, 10, 10, 10, 10, 5});
			CHECK_EQ(m.laChannel(0), uint8_t(0));
			send(m, 0x100004, {3, 3, 3, 3, 3, 3, 3, 3, 8, 5});
			CHECK_EQ(m.laChannel(0), uint8_t(5));
			// A reset sets the system area back, the parts keep receiving where they did until a system write
			send(m, g_allParametersReset, {0});
			CHECK_EQ(m.laChannel(0), uint8_t(1));
			CHECK_EQ(m.laReceiveChannel(0), uint8_t(5));
			m.processShort(0xc5, 9, 0);
			CHECK_EQ(pt(m, 0, 1), uint8_t(9));
			send(m, 0x100001, {1});
			CHECK_EQ(m.laReceiveChannel(0), uint8_t(1));
		}
		{
			// The master volume stays at a reset
			Memory m(DeviceModel::Cm32l);
			send(m, 0x100016, {50});
			send(m, g_allParametersReset, {0});
			CHECK_EQ(m.value(Area::LaSystem, 0, 0x16), uint8_t(50));
			CHECK_EQ(m.laReceiveChannel(0), uint8_t(1));
		}
		for(const auto model : {DeviceModel::Mt32New, DeviceModel::Cm32l})
		{
			// A message over two parts' patch temps: the CM-32L loads the second part's timbre when its number is
			// written, the MT-32 2.x never
			Memory m(model);
			const bool cm = model == DeviceModel::Cm32l;
			send(m, 0x03000e, {0, 0, 0, 5});
			CHECK(m.timbreSource(1) == (cm ? TimbreSource{0, 5} : TimbreSource{0, 48}));
			send(m, 0x03000f, {0, 1});
			CHECK(m.timbreSource(1) == (cm ? TimbreSource{0, 5} : TimbreSource{0, 48}));
			CHECK_EQ(pt(m, 1, 0), uint8_t(1));
			// Its restore puts the part back on the timbre it plays, not the one its patch temp selects
			Memory copy(model);
			for(const auto& msg : m.createRestore())
				send(copy, msg);
			CHECK(copy.timbreSource(1) == m.timbreSource(1));
			CHECK_EQ(copy.value(Area::PatchTemp, 1, 0), uint8_t(1));
		}
		{
			// The CM-32P rounds the level of CC#7 and keeps its RPN selection at a reset
			Memory m(DeviceModel::Cm32p);
			m.processShort(0xba, 7, 96);
			CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 0x14), uint8_t(76));
			m.processShort(0xba, 101, 0);
			m.processShort(0xba, 100, 0);
			send(m, g_allParametersReset, {0});
			m.processShort(0xba, 6, 5);
			CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 4), uint8_t(5));
		}
	}

	void testPresets()
	{
		Memory m(DeviceModel::Cm32l);
		// A data request reads part 1's timbre: a preset, b05. Part 3 plays it too once it selects it.
		std::vector<uint8_t> timbre(g_timbreSize, 0);
		timbre[0] = 'S';
		const auto reply = dt1(g_timbreTemp, timbre);
		CHECK(m.processReply(reply.data(), reply.size()));
		CHECK(m.entryKnown(Area::TimbreTemp, 0));
		CHECK(m.preset({1, 4}) != nullptr);
		send(m, 0x030020, {1, 4});
		CHECK(m.entryKnown(Area::TimbreTemp, 2));
		CHECK_EQ(m.value(Area::TimbreTemp, 2, 0), uint8_t('S'));
		// An edited timbre is no preset
		send(m, g_timbreTemp + 1, {'Z'});
		const auto reply2 = dt1(addressAdd(g_timbreTemp, 1), {'Z'});
		m.processReply(reply2.data(), reply2.size());
		CHECK_EQ((*m.preset({1, 4}))[1], uint8_t(0));
	}

	void testPcmRules()
	{
		Memory m(DeviceModel::Cm32p);
		send(m, addressAdd(g_pcmPatchMemory, 2 * g_pcmPatchMemorySize), {0, 77, 3});	// patch 3
		m.processShort(0xca, 2, 0);
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 1), uint8_t(77));
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 2), uint8_t(3));
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 0x14), uint8_t(80));
		m.processShort(0xba, 10, 100);
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 0x13), uint8_t(100));
		m.processShort(0xba, 101, 0);
		m.processShort(0xba, 100, 0);
		m.processShort(0xba, 6, 40);
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 4), uint8_t(12));
		// Internal media and a tone past the 84 internal ones: tone 85, in the patch temp only
		send(m, g_pcmPatchTemp, {0, 0x60});
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 1), uint8_t(85));
		send(m, g_pcmPatchTemp, {1, 0x60});
		send(m, g_pcmPatchTemp, {0});
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 1), uint8_t(85));
		send(m, g_pcmPatchMemory, {0, 0x60});
		CHECK_EQ(m.value(Area::PcmPatchMemory, 0, 1), uint8_t(0x60));
		m.processShort(0xca, 0, 0);
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 1), uint8_t(85));
		std::vector<uint8_t> all(g_pcmPatchTempSize, 0x7f);
		send(m, g_pcmPatchTemp, all);
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 0), uint8_t(1));
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 0x12), uint8_t(50));
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 0x14), uint8_t(100));
		// Any system write leaves part 1 a reserve of at most 31 and the others none, the firmware's bug
		CHECK_EQ(m.value(Area::PcmSystem, 0, 5), uint8_t(8));
		send(m, 0x520001, {1});
		CHECK_EQ(m.value(Area::PcmSystem, 0, 4), uint8_t(2));
		CHECK_EQ(m.value(Area::PcmSystem, 0, 5), uint8_t(0));
		send(m, 0x520004, {10, 10, 10, 10, 10, 10});
		CHECK_EQ(m.value(Area::PcmSystem, 0, 4), uint8_t(10));
		CHECK_EQ(m.value(Area::PcmSystem, 0, 5), uint8_t(0));
		send(m, 0x520004, {40});
		CHECK_EQ(m.value(Area::PcmSystem, 0, 4), uint8_t(31));
		// The first patch memory write copied the factory patches over it, 66 on in the wrong places
		CHECK_EQ(m.value(Area::PcmPatchMemory, 64, 1), uint8_t(0));
		const uint8_t patch66[] = {1, 1, 1, 0, 0x0c, 0x32};
		for(size_t i = 0; i < std::size(patch66); ++i)
			CHECK_EQ(m.value(Area::PcmPatchMemory, 65, i), patch66[i]);
		// Until a reset, after which the ROM's are back
		send(m, g_allParametersReset, {0});
		CHECK_EQ(m.value(Area::PcmPatchMemory, 65, 1), uint8_t(1));
		CHECK_EQ(m.value(Area::PcmPatchMemory, 65, 2), uint8_t(0x0c));
		// The LA board's addresses and channel IDs mean nothing to it
		CHECK(!send(m, 0x030000, {1}));
		CHECK(!send(m, g_pcmPatchTemp, {1}, 0x0a));
	}

	void testCm64()
	{
		// One stream for both halves: each takes its own addresses and channels, and one reset resets both
		Memory m(DeviceModel::Cm64);
		CHECK(m.hasLa() && m.hasPcm());
		send(m, 0x030002, {30});
		send(m, 0x500002, {20});
		CHECK_EQ(pt(m, 0, 2), uint8_t(30));
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 2), uint8_t(20));
		m.processShort(0xb1, 7, 127);
		m.processShort(0xba, 7, 0);
		CHECK_EQ(pt(m, 0, 8), uint8_t(100));
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 0x14), uint8_t(0));
		send(m, g_allParametersReset, {0});
		CHECK_EQ(pt(m, 0, 2), uint8_t(24));
		CHECK_EQ(m.value(Area::PcmPatchTemp, 0, 2), uint8_t(12));
	}

	// What a board that took the restore of _m has: the same, but for values that only a program change from the
	// CM-32P's misplaced card patches puts in its patch temps, which no data set can write
	bool sameAsRestored(const Memory& _restored, const Memory& _m)
	{
		for(size_t a = 0; a < static_cast<size_t>(Area::Count); ++a)
		{
			const auto area = static_cast<Area>(a);
			if((areaInfo(area).board == Board::La ? _m.hasLa() : _m.hasPcm()) == false)
				continue;
			for(size_t e = 0; e < _m.entryCount(area); ++e)
			{
				for(size_t i = 0; i < areaInfo(area).entrySize; ++i)
				{
					if(!_m.known(area, e, i) || !_restored.known(area, e, i))
						continue;
					auto v = _m.value(area, e, i);
					if(area == Area::PcmPatchTemp)
					{
						auto media = _m.value(area, e, 0);
						storeByte(Block::PcmPatchTemp, 0, LaFirmware::New, media);
						storeByte(Block::PcmPatchTemp, static_cast<uint8_t>(i), LaFirmware::New, v);
						if(i == 1 && !media && v >= 84)
							v = 85;
					}
					if(v != _restored.value(area, e, i))
						return false;
				}
			}
		}
		return true;
	}

	// Random traffic, then a board that took the restore of it has the same memory
	void testRestore(const DeviceModel _model, const uint32_t _seed)
	{
		std::mt19937 rng(_seed);
		Memory m(_model);
		const auto r = [&](const uint32_t _n) { return static_cast<uint8_t>(rng() % _n); };
		std::vector<uint32_t> bases;
		for(size_t a = 0; a < static_cast<size_t>(Area::Count); ++a)
		{
			const auto& info = areaInfo(static_cast<Area>(a));
			if((info.board == Board::La && m.hasLa()) || (info.board == Board::Pcm && m.hasPcm()))
				bases.push_back(info.address);
		}
		for(int i = 0; i < 400; ++i)
		{
			switch(r(5))
			{
			case 0:
			{
				std::vector<uint8_t> d(1 + r(12));
				for(auto& b : d)
					b = r(128);
				send(m, addressAdd(bases[r(static_cast<uint32_t>(bases.size()))], r(240)), d);
				break;
			}
			case 1: m.processShort(static_cast<uint8_t>(0xc0 | r(16)), r(128), 0); break;
			case 2: m.processShort(static_cast<uint8_t>(0xb0 | r(16)), r(2) ? 7 : 10, r(128)); break;
			case 3: send(m, addressAdd(g_channelTimbreTemp, r(250)), {r(128), r(128)}, r(16)); break;
			default:
				m.processShort(static_cast<uint8_t>(0xb0 | r(16)), 101, 0);
				m.processShort(static_cast<uint8_t>(0xb0 | r(16)), 100, 0);
				m.processShort(static_cast<uint8_t>(0xb0 | r(16)), 6, r(30));
				break;
			}
		}
		const auto restore = m.createRestore();
		Memory copy(_model);
		send(copy, dt1(0x030000, {3, 3}));		// whatever the board had before
		for(const auto& msg : restore)
			send(copy, msg);
		CHECK(sameAsRestored(copy, m));
		for(size_t p = 0; p < (m.hasLa() ? g_laTimbreParts : 0); ++p)
		{
			CHECK(copy.timbreSource(p) == m.timbreSource(p));
			CHECK_EQ(copy.entryKnown(Area::TimbreTemp, p), m.entryKnown(Area::TimbreTemp, p));
		}
		// Taking its own restore again changes nothing
		CHECK(copy.createRestore() == restore);
	}

	void testTracker()
	{
		StateTracker t;
		t.setModel(DeviceModel::Mt32New);
		const auto before = t.revision();
		t.process(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0x91, 60, 100));
		CHECK_EQ(t.revision(), before);
		t.process(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, 0xc1, 5, 0));
		CHECK(t.revision() != before);
		CHECK_EQ(t.memory().value(Area::PatchTemp, 0, 1), uint8_t(5));
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
		const auto msg = dt1(0x030002, {30});
		e.sysex.assign(msg.begin(), msg.end());
		t.process(e);
		CHECK_EQ(t.memory().value(Area::PatchTemp, 0, 2), uint8_t(30));
		CHECK(t.createRestore().size() > 1);
	}
}

int main()
{
	testMessages();
	testParams();
	testDefaults();
	for(const auto model : {DeviceModel::Mt32Old, DeviceModel::Mt32New, DeviceModel::Cm32l})
		testLaRules(model);
	testFirmwareQuirks();
	testPresets();
	testPcmRules();
	testCm64();
	for(const auto model : {DeviceModel::Mt32Old, DeviceModel::Cm32l, DeviceModel::Cm32p, DeviceModel::Cm64})
	{
		for(uint32_t seed = 1; seed <= 20; ++seed)
			testRestore(model, seed);
	}
	testTracker();
	return test::finish("mt32Memory");
}
