#include "cpu/common/test_util.hpp"
#include "romloader.h"
#include "sc55mk2.h"
#include "sc88.h"
#include "sc8850.h"
#include "synthLib/romLoader.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace emu88Lib
{
	struct VoiceReleaseTest
	{
		static void disable(Sc88& b) { b.m_releaseEg = 0; }
		static void disable(Sc8850& b) { b.m_autoVoiceReset = false; }
		static void disable(Sc55Mk2& b) { b.m_autoVoiceReset = false; }
		static bool enabled(const Sc88& b) { return b.m_releaseEg != 0; }
		static bool enabled(const Sc8850& b) { return b.m_autoVoiceReset; }
		static bool enabled(const Sc55Mk2& b) { return b.m_autoVoiceReset; }
		static void unknown(Sc88& b)
		{
			CHECK(b.isValid());
			CHECK(!enabled(b));
			for (const auto flags : {0x3bda, 0x3c56})
				b.extWrite8(0x80000 + flags, 0x80);
			for (const auto eg : {0x405a, 0x40d6})
			{
				b.extWrite8(0x80000 + eg, 0);
				b.extWrite8(0x80000 + eg + 1, 0);
			}
			for (const auto allocation : {0xf609, 0xf61d})
				b.extWrite8(0x80000 + allocation, 0xff);
			CHECK_EQ(b.m_pendingVoiceResets, uint64_t{0});
		}
		static void unknown(Sc55Mk2& b)
		{
			CHECK(b.isValid());
			CHECK(!enabled(b));
			b.extWrite8(0xadae, 0);
			b.extWrite8(0xadaf, 0x0c);
			b.extWrite8(0xadaf, 0x16);
			CHECK_EQ(b.m_pendingVoiceResets, 0u);
			CHECK_EQ(b.m_gp.retiredVoices(), 0u);
		}
		static void unknown(Sc8850& b)
		{
			CHECK(b.isValid());
			CHECK(!enabled(b));
			for (auto& xp : b.m_xp)
			{
				xp.hostWrite(0x3900, 1);
				(void)xp.hostRead(0x3900);
			}
			for (const auto voice : {0u, 64u})
			{
				auto* record = b.m_machine.bus().ptr(Sc8850::WorkRamBase + 0x22024 + voice * 0x1e4);
				record[0] = voice;
				record[1] = 1;
				record[0x148] = 0;
				record[0x149] = 1;
			}
			b.resetFinishedVoices();
			CHECK_EQ(live(b), 2u);
		}
		static void unknownRoms()
		{
			for (const auto model : {Model::Sc88, Model::Sc88VL})
			{
				Sc88 b(std::vector<uint8_t>(Sc88::RomSize), {}, model, false);
				unknown(b);
			}
			{
				Sc55RomSet roms;
				roms.internalRom.resize(Sc55Mk2::InternalRomSize);
				roms.programRom.resize(Sc55Mk2::ProgramRomSize);
				Sc55Mk2 b(std::move(roms));
				unknown(b);
			}
			{
				Sc8850 b(std::vector<uint8_t>(Sc8850::CpuRomSize), std::vector<uint8_t>(Sc8850::ProgramRomSize),
						 std::vector<uint8_t>(Sc8850::DataRomSize), {}, {}, false);
				unknown(b);
			}
		}
		static unsigned live(const xpLib::XP& xp)
		{
			unsigned count = 0;
			for (const auto& voice : xp.state().voices)
				count += voice.resetState_3900.released;
			return count;
		}
		static unsigned live(const Sc88& b) { return live(b.m_xp); }
		static unsigned live(const Sc8850& b) { return live(b.m_xp[0]) + live(b.m_xp[1]); }
		static unsigned live(const Sc55Mk2& b)
		{
			const auto& regs = b.m_gp.regs();
			return __builtin_popcount(regs.voice_mask & regs.voice_mask_pending & ~b.m_gp.retiredVoices() & 0xfffffff);
		}
		static void checkSoundOff(const Sc88& b, const std::pair<double, double>&) { CHECK_EQ(live(b), 0u); }
		static void checkSoundOff(const Sc8850& b, const std::pair<double, double>&) { CHECK_EQ(live(b), 0u); }
		static void checkSoundOff(const Sc55Mk2& b, const std::pair<double, double>& energy)
		{
			CHECK_EQ(energy.first, 0.0);
			CHECK_EQ(energy.second, 0.0);
			// Forced-stop requests can remain pending in the unmodified firmware too.
			const auto mask = b.m_gp.regs().voice_mask & ~b.m_gp.retiredVoices();
			for (unsigned voice = 0; voice < 28; ++voice)
			{
				if (!(mask & (uint32_t{1} << voice)))
					continue;
				const auto base = 0x2dae + voice * 0x12a;
				const auto state = (b.m_sram[base] << 8) | b.m_sram[base + 1];
				CHECK(state == 0x12 || state == 0x14);
			}
		}
		static unsigned auxiliary(const Sc88&) { return 0; }
		static unsigned auxiliary(const Sc55Mk2&) { return 0; }
		static unsigned auxiliary(const Sc8850& b) { return live(b.m_xp[0]); }
		static void send(Sc88& b, const synthLib::SMidiEvent& e) { b.addMidiEvent(e, e.port); }
		static void send(Sc8850& b, const synthLib::SMidiEvent& e)
		{
			const uint8_t bytes[]{e.a, e.b, e.c};
			b.usbMidiIn(e.port, bytes, (e.a & 0xf0) == 0xc0 ? 2 : 3);
		}
		static void send(Sc55Mk2& b, const synthLib::SMidiEvent& e)
		{
			const uint8_t bytes[]{e.a, e.b, e.c};
			b.sendMidiBytes(bytes, (e.a & 0xf0) == 0xc0 ? 2 : 3, Sc55Mk2::MidiInA);
		}

		template <class Board>
		static void run(Board& actual, Board& reference, const char* name, unsigned rate, unsigned ports)
		{
			using namespace test;
			CHECK(actual.isValid() && reference.isValid());
			CHECK(enabled(actual));
			disable(reference);
			unsigned peak = 0, auxiliaryPeak = 0;
			const auto midi = [&](uint8_t a, uint8_t b, uint8_t c, uint8_t port = 0)
			{
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Physical, a, b, c);
				event.port = port;
				send(actual, event);
				send(reference, event);
			};
			const auto render = [&](unsigned frames)
			{
				double actualEnergy = 0, referenceEnergy = 0;
				for (unsigned i = 0; i < frames; ++i)
				{
					const auto a = actual.renderSample(), r = reference.renderSample();
					actualEnergy += double(a.first) * a.first + double(a.second) * a.second;
					referenceEnergy += double(r.first) * r.first + double(r.second) * r.second;
					if ((i & 127u) == 0)
					{
						peak = std::max(peak, live(actual));
						auxiliaryPeak = std::max(auxiliaryPeak, auxiliary(actual));
					}
				}
				return std::pair{actualEnergy, referenceEnergy};
			};
			const auto audible = [&](unsigned frames)
			{
				const auto [a, r] = render(frames);
				CHECK(r > 0);
				CHECK(a > r * 0.8 && a < r * 1.2);
				if (!(a > r * 0.8 && a < r * 1.2))
					std::cout << name << " energy ratio=" << a / r << std::endl;
			};
			const auto benchmark = [&](const char* phase)
			{
				if (!std::getenv("EMU88_IDLE_DIAGNOSTICS"))
					return;
				const auto measure = [&](Board& board)
				{
					const auto start = std::chrono::steady_clock::now();
					for (unsigned i = 0; i < 2 * rate; ++i)
						board.renderSample();
					return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				};
				std::cout << name << " " << phase << " enabled_ms=" << measure(actual)
						  << " disabled_ms=" << measure(reference) << std::endl;
			};
			render(8 * rate);
			benchmark("fresh");
			for (unsigned port = 0; port < ports; ++port)
			{
				midi(0xc0, 16, 0, port);
				midi(0x90, 60, 100, port);
				audible(rate);
				midi(0xb0, 7, 0, port);
				render(2 * rate);
				CHECK(live(actual) > 0);
				midi(0xb0, 7, 100, port);
				midi(0xb0, 11, 0, port);
				render(rate);
				CHECK(live(actual) > 0);
				midi(0xb0, 11, 127, port);
				audible(rate);
				midi(0xb0, 64, 127, port);
				midi(0x80, 60, 64, port);
				audible(2 * rate);
				CHECK(live(actual) > 0);
				midi(0xb0, 64, 0, port);
				render(8 * rate);
				CHECK_EQ(live(actual), 0u);
				std::cout << name << " pedal/volume port=" << port << " active=" << live(actual) << std::endl;
				midi(0xc0, 0, 0, port);
			}
			midi(0xc0, 48, 0);
			midi(0x90, 60, 100);
			audible(2 * rate);
			midi(0xb0, 66, 127);
			render(rate / 4);
			midi(0x80, 60, 64);
			audible(2 * rate);
			CHECK(live(actual) > 0);
			midi(0xb0, 66, 0);
			render(8 * rate);
			CHECK_EQ(live(actual), 0u);
			midi(0x90, 64, 100);
			audible(rate);
			midi(0xb0, 120, 0);
			render(8 * rate);
			const auto offEnergy = render(rate);
			checkSoundOff(actual, offEnergy);
			midi(0xc0, 0, 0);

			peak = auxiliaryPeak = 0;
			std::ifstream input(EMU88_TEST_DATA_DIR "/voice-reuse.txt");
			CHECK(input.is_open());
			std::string line;
			unsigned frame = 0, count = 0;
			while (std::getline(input, line))
			{
				if (line.empty() || line[0] == '#')
					continue;
				std::istringstream row(line);
				unsigned target, status, key, velocity;
				if (!(row >> target >> status >> key >> velocity))
				{
					CHECK(false);
					break;
				}
				const auto next = static_cast<unsigned>(uint64_t(target) * rate / 32000);
				render(next - frame);
				frame = next;
				midi(status, key, velocity);
				++count;
			}
			CHECK_EQ(count, 1324u);
			render(8 * rate);
			CHECK_EQ(live(actual), 0u);
			std::cout << name << " stress peak=" << peak << " auxiliary peak=" << auxiliaryPeak
					  << " remaining=" << live(actual) << " reference=" << live(reference) << std::endl;
			if (ports == 4)
				CHECK(auxiliaryPeak > 0);
			benchmark("after stress");
			for (unsigned interval : {rate / 2, rate * 2})
			{
				for (unsigned note = 0; note < 40; ++note)
				{
					midi(0x90, 60, 40 + (note * 17) % 88);
					audible(interval / 2);
					midi(0x80, 60, 64);
					render(interval / 2);
				}
			}
			render(8 * rate);
			CHECK_EQ(live(actual), 0u);
			std::cout << name << " completed" << std::endl;
		}

		static void runModel(const std::string& model)
		{
			if (model == "88" || model == "88vl")
			{
				const auto type = model == "88" ? Model::Sc88 : Model::Sc88VL;
				auto rom = RomLoader::findROM(type);
				auto wave = RomLoader::findWaveRom();
				auto firmware = rom.takeData(), samples = wave.takeData();
				CHECK(!firmware.empty());
				if (firmware.empty())
					return;
				{
					const auto other = type == Model::Sc88 ? Model::Sc88VL : Model::Sc88;
					Sc88 mismatched(firmware, {}, other, false);
					unknown(mismatched);
					auto changed = firmware;
					changed.back() ^= 1;
					Sc88 modified(std::move(changed), {}, type, false);
					unknown(modified);
				}
				Sc88 actual(firmware, samples, type), reference(firmware, samples, type);
				run(actual, reference, model.c_str(), 32000, 2);
			}
			else if (model == "55")
			{
				auto roms = RomLoader::findSc55RomSet();
				CHECK(!roms.internalRom.empty() && !roms.programRom.empty());
				if (roms.internalRom.empty() || roms.programRom.empty())
					return;
				for (unsigned part = 0; part < 3; ++part)
				{
					Sc55RomSet changed;
					changed.internalRom = roms.internalRom;
					changed.programRom = roms.programRom;
					if (part == 2)
						changed.programRom.resize(Sc55Mk2::ProgramRomSize + 1);
					else
						(part == 0 ? changed.internalRom : changed.programRom).back() ^= 1;
					Sc55Mk2 modified(std::move(changed));
					unknown(modified);
				}
				Sc55Mk2 actual(roms), reference(roms);
				actual.setSwitchPosition(Sc55Mk2::SwitchMidi);
				reference.setSwitchPosition(Sc55Mk2::SwitchMidi);
				run(actual, reference, "55", Sc55Mk2::SampleRate, 1);
			}
			else
			{
				auto roms = RomLoader::findSc8850RomSet();
				auto waves = RomLoader::findSc8850WaveRomSet();
				CHECK(!roms.cpu.empty() && !roms.program.empty() && !roms.data.empty());
				if (roms.cpu.empty() || roms.program.empty() || roms.data.empty())
					return;
				for (unsigned part = 0; part < 3; ++part)
				{
					auto changed = roms;
					(part == 0 ? changed.cpu : part == 1 ? changed.program : changed.data).back() ^= 1;
					Sc8850 modified(std::move(changed.cpu), std::move(changed.program), std::move(changed.data), {}, {},
									false);
					unknown(modified);
				}
				Sc8850 actual(roms.cpu, roms.program, roms.data, waves.romA, waves.romB);
				Sc8850 reference(roms.cpu, roms.program, roms.data, waves.romA, waves.romB);
				run(actual, reference, "8850", 32000, 4);
			}
		}
	};
} // namespace emu88Lib
int main(int argc, char** argv)
{
	using namespace test;
	emu88Lib::VoiceReleaseTest::unknownRoms();
	gpLib::GP gp;
	gp.write8(3, 1);
	(void)gp.read8(0);
	gp.retireVoice(0);
	CHECK_EQ(gp.retiredVoices(), 1u);
	gp.write8(3, 3);
	(void)gp.read8(0);
	CHECK_EQ(gp.retiredVoices(), 1u);
	CHECK_EQ(gp.regs().voice_mask, 3u);
	gp.write8(3, 2);
	gp.write8(3, 3);
	CHECK_EQ(gp.retiredVoices(), 0u);
	gp.retireVoice(0);
	gp.retireVoice(28);
	CHECK_EQ(gp.retiredVoices(), 1u);
	gp.reset();
	CHECK_EQ(gp.retiredVoices(), 0u);
	if (argc == 3)
	{
		const std::string model(argv[2]);
		if (model != "88" && model != "88vl" && model != "55" && model != "8850")
			return 2;
		synthLib::RomLoader::addSearchPath(argv[1], true);
		emu88Lib::VoiceReleaseTest::runModel(model);
	}
	else if (argc != 1)
		return 2;
	return test::finish("voiceRelease");
}
