// Optional integration test: needs the boards' ROMs, from argv[1] or EMU88_MT32_TEST_ROM_DIR.
//
// Random MIDI goes into an emulated board and into mt32::Memory alike; what the board then has - read back with data
// requests on the LA boards, from its RAM on the CM-32P, which answers none - has to match every byte the copy
// claims to know. Then the copy's restore goes into a freshly booted board, which has to end up with the same
// memory as the first.
#include "88lib/boards/cm32p.h"
#include "88lib/boards/laBoard.h"
#include "88lib/mt32/memory.h"
#include "88lib/mt32/params.h"
#include "88lib/rom/romloader.h"
#include "baseLib/os.h"
#include "cpu/common/test_util.hpp"
#include "synthLib/romLoader.h"

#include <cstdlib>
#include <random>

using namespace emu88Lib;
using namespace emu88Lib::mt32;

namespace
{
	constexpr uint32_t g_sampleRate = 32000;

	template<class Board> struct Driver
	{
		Board& board;
		std::vector<synthLib::SMidiEvent> out;

		void run(const uint32_t _samples)
		{
			for(uint32_t i = 0; i < _samples; ++i)
			{
				board.renderSample();
				if((i & 255) == 255)
					board.readMidiOut(out);
			}
			board.readMidiOut(out);
		}
		void send(const Message& _m, const uint32_t _wait)
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			e.sysex.assign(_m.begin(), _m.end());
			board.addMidiEvent(e);
			run(_wait);
		}
		void send(const uint8_t _a, const uint8_t _b, const uint8_t _c, const uint32_t _wait)
		{
			board.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, _a, _b, _c));
			run(_wait);
		}
		// Time for a message on the board's MIDI IN, 320 us a byte, and for the firmware to take it. The boards have
		// no flow control: what comes while the firmware is still busy with the last message can be lost, as on the
		// hardware - a lost status byte turns the data after it into a message of the status before - so the
		// traffic leaves them 40 ms and a millisecond per byte.
		static uint32_t waitFor(const size_t _bytes)
		{
			return static_cast<uint32_t>(_bytes * (11 + g_sampleRate / 1000) + g_sampleRate / 25);
		}
	};

	// ---- LA boards

	struct LaDump
	{
		std::vector<Message> replies;
		std::vector<uint8_t> bytes;		// by linear address from 03 00 00 on; 0xff = not answered
	};

	// Everything the LA board has, read with data requests
	LaDump readLa(Driver<LaBoard>& _d, const LaFirmware _firmware)
	{
		LaDump dump;
		dump.bytes.assign(toLinear(g_laSystem) + g_laSystemSize, 0xff);
		const std::pair<uint32_t, uint32_t> ranges[] = {
			{g_patchTemp, static_cast<uint32_t>(g_laParts * g_patchTempSize + rhythmKeyCount(_firmware) * 4)},
			{g_timbreTemp, static_cast<uint32_t>(g_laTimbreParts * g_timbreSize)},
			{g_patchMemory, static_cast<uint32_t>(g_patchMemoryCount * g_patchMemorySize)},
			{g_timbreMemory, static_cast<uint32_t>(g_timbreMemoryCount * g_timbreMemoryStride)},
			{g_laSystem, static_cast<uint32_t>(g_laSystemSize)},
		};
		for(const auto& [address, size] : ranges)
		{
			_d.out.clear();
			_d.send(rq1(address, size), 320);
			size_t got = 0;
			uint32_t silent = 0;
			while(got < size && silent < g_sampleRate / 2)
			{
				const auto before = _d.out.size();
				_d.run(320);
				silent = _d.out.size() == before ? silent + 320 : 0;
				got = 0;
				for(const auto& e : _d.out)
				{
					Dt1 dt1;
					if(!e.sysex.empty() && parseDt1(e.sysex.data(), e.sysex.size(), dt1))
						got += dt1.size;
				}
			}
			CHECK_EQ(got, size_t(size));
			for(const auto& e : _d.out)
			{
				Dt1 dt1;
				if(e.sysex.empty() || !parseDt1(e.sysex.data(), e.sysex.size(), dt1))
					continue;
				dump.replies.emplace_back(e.sysex.begin(), e.sysex.end());
				for(size_t i = 0; i < dt1.size; ++i)
				{
					const auto at = toLinear(dt1.address) + i;
					if(at < dump.bytes.size())
						dump.bytes[at] = dt1.data[i];
				}
			}
		}
		return dump;
	}

	// The known bytes of the copy against the board; prints the first few that differ
	size_t compareLa(const Memory& _m, const LaDump& _dump, const char* _when)
	{
		size_t mismatches = 0;
		for(size_t a = 0; a < static_cast<size_t>(Area::Count); ++a)
		{
			const auto area = static_cast<Area>(a);
			const auto& info = areaInfo(area);
			if(info.board != Board::La)
				continue;
			for(size_t e = 0; e < _m.entryCount(area); ++e)
			{
				for(size_t i = 0; i < info.entrySize; ++i)
				{
					if(!_m.known(area, e, i))
						continue;
					const auto at = toLinear(info.address) + e * info.entryStride + i;
					if(_dump.bytes[at] == _m.value(area, e, i))
						continue;
					if(mismatches++ < 8)
						std::printf("  %s: %s %zu +%02zx: board %02x, copy %02x\n", _when, info.name, e, i,
							_dump.bytes[at], _m.value(area, e, i));
				}
			}
		}
		return mismatches;
	}

	// ---- the CM-32P, by its RAM

	struct PcmRam
	{
		std::vector<uint8_t> system, patchTemp, patchMemory;
	};

	constexpr uint16_t g_ramSystem = 0x23c4, g_ramPatchTemp = 0x2344, g_ramPatchMemory = 0x2b5a;
	constexpr size_t g_ramPatches = g_pcmPatchMemoryCount;

	PcmRam readPcm(Cm32p& _b)
	{
		const auto read = [&](const uint16_t _address, const size_t _size)
		{
			std::vector<uint8_t> v(_size);
			for(size_t i = 0; i < _size; ++i)
				v[i] = _b.ramByte(static_cast<uint16_t>(_address + i));
			return v;
		};
		return {read(g_ramSystem, g_pcmSystemSize), read(g_ramPatchTemp, g_pcmParts * g_pcmPatchTempSize),
			read(g_ramPatchMemory, g_ramPatches * g_pcmPatchMemorySize)};
	}

	// After power-on and a reset the patch memory is the ROM's, and the RAM has whatever it had before, until the
	// first write there copies the factory patches to it
	size_t comparePcm(const Memory& _m, const PcmRam& _ram, const bool _patchMemoryInRam, const char* _when)
	{
		size_t mismatches = 0;
		const auto check = [&](const char* _what, const uint8_t _board, const uint8_t _copy, const size_t _at)
		{
			if(_board != _copy && mismatches++ < 8)
				std::printf("  %s: %s +%02zx: board %02x, copy %02x\n", _when, _what, _at, _board, _copy);
		};
		for(size_t i = 0; i < g_pcmSystemSize; ++i)
			check("system", _ram.system[i], _m.value(Area::PcmSystem, 0, i), i);
		for(size_t p = 0; p < g_pcmParts; ++p)
		{
			for(size_t i = 0; i < g_pcmPatchTempSize; ++i)
				check("patch temp", _ram.patchTemp[p * g_pcmPatchTempSize + i], _m.value(Area::PcmPatchTemp, p, i),
					p * g_pcmPatchTempSize + i);
		}
		if(_patchMemoryInRam)
		{
			for(size_t n = 0; n < g_ramPatches; ++n)
			{
				for(size_t i = 0; i < g_pcmPatchMemorySize; ++i)
					check("patch memory", _ram.patchMemory[n * g_pcmPatchMemorySize + i],
						_m.value(Area::PcmPatchMemory, n, i), n * g_pcmPatchMemorySize + i);
			}
		}
		return mismatches;
	}

	// ---- traffic

	struct Traffic
	{
		std::mt19937 rng;
		bool la, pcm;
		LaFirmware laFirmware = LaFirmware::New;

		uint8_t r(const uint32_t _n) { return static_cast<uint8_t>(rng() % _n); }

		// A message that addresses the memory, or a channel message that writes to it
		synthLib::SMidiEvent next()
		{
			synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
			const auto choice = r(100);
			if(choice < 45)
			{
				std::vector<Area> areas;
				for(size_t a = 0; a < static_cast<size_t>(Area::Count); ++a)
				{
					const auto& info = areaInfo(static_cast<Area>(a));
					if((info.board == Board::La && la) || (info.board == Board::Pcm && pcm))
						areas.push_back(static_cast<Area>(a));
				}
				// Within the area: the firmware does not check every write that runs past one, see Memory
				const auto& info = areaInfo(areas[r(static_cast<uint32_t>(areas.size()))]);
				const auto count = entryCount(info.area, la ? laFirmware : LaFirmware::New);
				const auto entry = r(count);
				const auto offset = r(info.entrySize);
				const size_t room = (count - entry - 1) * info.entryStride + info.entrySize - offset;
				std::vector<uint8_t> data(std::min(room, size_t(r(5) ? 1 + r(6) : info.entrySize)));
				for(auto& b : data)
				{
					// Mostly in range, some far out
					b = r(4) ? r(16) : r(128);
				}
				// Channels in range, so that the parts keep receiving
				if(info.area == Area::LaSystem || info.area == Area::PcmSystem)
				{
					for(auto& b : data)
						b = r(17);
				}
				const auto m = dt1(addressAdd(info.address, entry * info.entryStride + offset), data);
				e.sysex.assign(m.begin(), m.end());
			}
			else if(choice < 50 && la)
			{
				const auto m = dt1(addressAdd(g_channelTimbreTemp, r(250)), {r(128), r(16)}, r(16));
				e.sysex.assign(m.begin(), m.end());
			}
			else if(choice < 51)
			{
				const auto m = dt1(g_allParametersReset, std::vector<uint8_t>{0});
				e.sysex.assign(m.begin(), m.end());
			}
			else if(choice < 70)
			{
				e.a = static_cast<uint8_t>(0xc0 | r(16));
				e.b = r(128);
			}
			else
			{
				static constexpr uint8_t ccs[] = {7, 10, 101, 100, 6, 99};
				e.a = static_cast<uint8_t>(0xb0 | r(16));
				e.b = ccs[r(static_cast<uint32_t>(std::size(ccs)))];
				e.c = e.b == 101 || e.b == 100 ? (r(4) ? 0 : r(128)) : r(128);
			}
			return e;
		}
	};

	void apply(Memory& _m, const synthLib::SMidiEvent& _e)
	{
		if(_e.sysex.empty())
			_m.processShort(_e.a, _e.b, _e.c);
		else
			_m.processSysex(_e.sysex.data(), _e.sysex.size());
	}

	// As a host would send it: a pause after the reset and after the first patch memory write
	template<class Board> void sendRestore(Driver<Board>& _d, const std::vector<Message>& _restore)
	{
		bool patchMemory = false;
		for(size_t i = 0; i < _restore.size(); ++i)
		{
			Dt1 dt1;
			parseDt1(_restore[i].data(), _restore[i].size(), dt1);
			const bool pause = !i || (!patchMemory && (dt1.address >> 16) == (g_pcmPatchMemory >> 16));
			patchMemory |= (dt1.address >> 16) == (g_pcmPatchMemory >> 16);
			const auto wait = Driver<Board>::waitFor(_restore[i].size());
			_d.send(_restore[i], pause ? wait + g_restoreResetDelayMs * g_sampleRate / 1000 : wait);
		}
		_d.run(g_sampleRate / 2);
	}

	void crosscheckLa(const DeviceModel _model)
	{
		const auto roms = RomLoader::findLaRomSet(toLaModel(_model));
		LaBoard board(roms);
		if(!board.isValid())
		{
			std::printf("%s: no ROMs, skipped\n", getDeviceProfile(_model).displayName);
			return;
		}
		Driver<LaBoard> d{board};
		d.run(g_sampleRate * 3);
		Memory m(_model);
		CHECK_EQ(compareLa(m, readLa(d, m.firmware()), "power-on"), size_t(0));

		Traffic traffic{std::mt19937(static_cast<uint32_t>(_model) * 7919u), true, false, m.firmware()};
		for(int round = 0; round < 8; ++round)
		{
			for(int i = 0; i < 30; ++i)
			{
				const auto e = traffic.next();
				apply(m, e);
				// The MT-32 1.x loses messages at this pace that the newer firmware takes; it is known for wanting
				// pauses between SysEx messages
				const auto pace = m.firmware() == LaFirmware::Old ? 4u : 1u;
				if(e.sysex.empty())
					d.send(e.a, e.b, e.c, Driver<LaBoard>::waitFor(3) * pace);
				else
					d.send(Message(e.sysex.begin(), e.sysex.end()), Driver<LaBoard>::waitFor(e.sysex.size()) * pace);
			}
			const auto dump = readLa(d, m.firmware());
			char when[32];
			std::snprintf(when, sizeof(when), "round %d", round);
			CHECK_EQ(compareLa(m, dump, when), size_t(0));
			// What the board answered makes the copy whole, and teaches it the presets it shows
			for(const auto& reply : dump.replies)
				m.processReply(reply.data(), reply.size());
			CHECK(m.unknownRanges().empty());
		}

		const auto original = readLa(d, m.firmware());
		LaBoard fresh(roms);
		Driver<LaBoard> f{fresh};
		f.run(g_sampleRate * 3);
		sendRestore(f, m.createRestore());
		const auto restored = readLa(f, m.firmware());
		size_t differences = 0;
		for(size_t i = 0; i < original.bytes.size(); ++i)
		{
			if(original.bytes[i] != restored.bytes[i] && differences++ < 8)
				std::printf("  restore: %06x: board %02x, restored %02x\n", fromLinear(static_cast<uint32_t>(i)),
					original.bytes[i], restored.bytes[i]);
		}
		CHECK_EQ(differences, size_t(0));
		std::printf("%s: crosschecked\n", getDeviceProfile(_model).displayName);
	}

	void crosscheckPcm()
	{
		const auto roms = RomLoader::findCm32pRomSet();
		Cm32p board(roms);
		if(!board.isValid())
		{
			std::printf("CM-32P: no ROMs, skipped\n");
			return;
		}
		Driver<Cm32p> d{board};
		d.run(g_sampleRate * 3);
		Memory m(DeviceModel::Cm32p);
		bool patchMemoryInRam = false;
		CHECK_EQ(comparePcm(m, readPcm(board), patchMemoryInRam, "power-on"), size_t(0));

		Traffic traffic{std::mt19937(4242u), false, true};
		for(int round = 0; round < 8; ++round)
		{
			for(int i = 0; i < 30; ++i)
			{
				const auto e = traffic.next();
				apply(m, e);
				if(e.sysex.empty())
				{
					d.send(e.a, e.b, e.c, Driver<Cm32p>::waitFor(3));
					continue;
				}
				// After power-on and after a reset the patch memory is the ROM's; the first write copies it to the RAM,
				// which takes the firmware a while before it takes the next message
				Dt1 dt1;
				auto wait = Driver<Cm32p>::waitFor(e.sysex.size());
				if(parseDt1(e.sysex.data(), e.sysex.size(), dt1))
				{
					const auto top = dt1.address >> 16;
					if(top == (g_pcmPatchMemory >> 16) && !patchMemoryInRam)
					{
						patchMemoryInRam = true;
						wait += g_restoreResetDelayMs * g_sampleRate / 1000;
					}
					if(top == (g_allParametersReset >> 16))
						patchMemoryInRam = false;
				}
				d.send(Message(e.sysex.begin(), e.sysex.end()), wait);
			}
			char when[32];
			std::snprintf(when, sizeof(when), "round %d", round);
			CHECK_EQ(comparePcm(m, readPcm(board), patchMemoryInRam, when), size_t(0));
		}

		Cm32p fresh(roms);
		Driver<Cm32p> f{fresh};
		f.run(g_sampleRate * 3);
		sendRestore(f, m.createRestore());
		// A program change from one of the misplaced card patches puts values out of range in a patch temp, which
		// no data set can write: the restored board has them clamped, as a data set of them would
		auto a = readPcm(board);
		const auto b = readPcm(fresh);
		for(size_t p = 0; p < g_pcmParts; ++p)
		{
			auto* patch = a.patchTemp.data() + p * g_pcmPatchTempSize;
			for(size_t i = 0; i < g_pcmPatchTempSize; ++i)
				storeByte(Block::PcmPatchTemp, static_cast<uint8_t>(i), LaFirmware::New, patch[i]);
			if(!patch[0] && patch[1] >= 84)
				patch[1] = 85;
		}
		CHECK(a.patchTemp == b.patchTemp);
		CHECK(!patchMemoryInRam || a.patchMemory == b.patchMemory);
		CHECK(a.system == b.system);
		std::printf("CM-32P: crosschecked\n");
	}
}

int main(int _argc, char** _argv)
{
	baseLib::disableErrorDialogs();
	const char* folder = _argc > 1 ? _argv[1] : std::getenv("EMU88_MT32_TEST_ROM_DIR");
	if(!folder)
		return 77;
	synthLib::RomLoader::setSearchPath(folder);
	for(const auto model : {DeviceModel::Mt32Old, DeviceModel::Mt32New, DeviceModel::Cm32l})
		crosscheckLa(model);
	crosscheckPcm();
	return test::finish("mt32Firmware");
}
