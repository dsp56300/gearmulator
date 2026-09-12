#include "cpu/common/test_util.hpp"
#include "romloader.h"
#include "sc88pro.h"
#include "synthLib/romLoader.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace emu88Lib
{
	struct Sc88ProReleaseTest
	{
		static void reuse(const char* _romPath, const unsigned _halfPeriod)
		{
			using namespace test;
			synthLib::RomLoader::addSearchPath(_romPath, true);
			const auto roms = RomLoader::findSc88ProRomSet();
			CHECK(roms.isValid());
			if (!roms.isValid())
				return;
			const bool diagnostics = std::getenv("EMU88_IDLE_DIAGNOSTICS") != nullptr;
			std::vector<uint8_t> waves;
			for (const auto* chip : {&roms.waveA, &roms.waveB, &roms.waveC})
				waves.insert(waves.end(), chip->begin(), chip->end());
			Sc88Pro actual(roms.firmware, waves), reference(roms.firmware, waves);
			CHECK(actual.m_autoVoiceReset);
			reference.m_autoVoiceReset = false;
			const auto render = [&](const unsigned _frames)
			{
				for (unsigned i = 0; i < _frames; ++i)
				{
					actual.renderSample();
					reference.renderSample();
				}
			};
			const auto send = [&](const unsigned _status, const unsigned _key, const unsigned _velocity)
			{
				const synthLib::SMidiEvent event(synthLib::MidiEventSource::Physical, _status, _key, _velocity);
				actual.addMidiEvent(event, 0);
				reference.addMidiEvent(event, 0);
			};
			render(8 * 32000);
			send(0xc0, 0, 0); // Piano 1
			render(32000);
			const auto idleStats = [&](const char* label)
			{
				if (!diagnostics)
					return;
				unsigned active = 0, egZero = 0, ampZero = 0, firmwareFree = 0;
				for (unsigned v = 0; v < 64; ++v)
				{
					const auto& voice = actual.m_xp.state().voices[v];
					if (!voice.resetState_3900.released)
						continue;
					++active;
					egZero += !(actual.m_sram[actual.m_releaseEg + 2 * v]
						|| actual.m_sram[actual.m_releaseEg + 2 * v + 1]);
					ampZero += voice.ampCurrent_1e00 == 0;
					firmwareFree += actual.m_sram[actual.m_voiceAllocation + 2 * v] == 0xff;
				}
				std::cout << "Idle " << label << " active=" << active << " egZero=" << egZero << " ampZero=" << ampZero
						  << " firmwareFree=" << firmwareFree << std::endl;
			};
			idleStats("boot");

			// Real keyboard input that exhausted the allocator. Starting from a
			// clean boot and repeating just one key does not reproduce this bug.
			std::ifstream input(EMU88_TEST_DATA_DIR "/voice-reuse.txt");
			CHECK(input.is_open());
			if (!input.is_open())
				return;
			std::string line;
			unsigned frame = 0, count = 0;
			while (std::getline(input, line))
			{
				if (line.empty() || line[0] == '#')
					continue;
				std::istringstream row(line);
				unsigned target, status, key, velocity;
				if (!(row >> target >> status >> key >> velocity) || target < frame)
				{
					CHECK(false);
					return;
				}
				render(target - frame);
				frame = target;
				send(status, key, velocity);
				++count;
			}
			CHECK_EQ(count, 1324u);
			render(4 * 32000);
			idleStats("after stress");
			CHECK_EQ(live(actual), uint64_t{0});
			unsigned failures = 0;
			for (unsigned note = 0; note < 80; ++note)
			{
				render((note * 37) % 251);
				send(0x90, 60, 40 + (note * 17) % 88);
				double actualEnergy = 0, referenceEnergy = 0;
				for (unsigned i = 0; i < _halfPeriod; ++i)
				{
					const auto a = actual.renderSample(), r = reference.renderSample();
					actualEnergy += double(a.first) * a.first + double(a.second) * a.second;
					referenceEnergy += double(r.first) * r.first + double(r.second) * r.second;
				}
				// Residual reverb is nonzero even when a new note is lost. Compare
				// each attack against the identical input with retirement disabled.
				const bool audible = referenceEnergy > 0 && actualEnergy > referenceEnergy * 0.8;
				CHECK(audible);
				if (!audible)
				{
					++failures;
					std::cout << "Missing attack " << note << ", ratio=" << actualEnergy / referenceEnergy << std::endl;
				}
				send(0x80, 60, 64);
				render(_halfPeriod);
			}
			std::cout << "Voice reuse: " << count
					  << " stress events, 80 attacks, interval=" << double(_halfPeriod * 2) / 32000
					  << " s, missing=" << failures << std::endl;
			render(4 * 32000);
			CHECK_EQ(live(actual), uint64_t{0});
			if (diagnostics)
			{
				render(4 * 32000);
				const auto bench = [](Sc88Pro& board)
				{
					const auto start = std::chrono::steady_clock::now();
					for (unsigned i = 0; i < 32000 * 2; ++i)
						board.renderSample();
					return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				};
				Sc88Pro fresh(roms.firmware, waves);
				for (unsigned i = 0; i < 9 * 32000; ++i)
					fresh.renderSample();
				std::cout << "Idle benchmark, 2 seconds: fresh=" << bench(fresh) << " ms, retirement=" << bench(actual)
						  << " ms, no retirement=" << bench(reference) << " ms" << std::endl;
			}
		}
		static uint64_t live(const Sc88Pro& b)
		{
			uint64_t result = 0;
			for (unsigned i = 0; i < 64; ++i)
				if (b.m_xp.state().voices[i].resetState_3900.released)
					result |= uint64_t{1} << i;
			return result;
		}
		static void run(const char* path)
		{
			using namespace test;
			synthLib::RomLoader::addSearchPath(path, true);
			auto roms = RomLoader::findSc88ProRomSet();
			CHECK(roms.isValid());
			if (!roms.isValid())
				return;
			std::vector<uint8_t> waves;
			for (auto* chip : {&roms.waveA, &roms.waveB, &roms.waveC})
				waves.insert(waves.end(), chip->begin(), chip->end());
			struct Case
			{
				const char* name;
				unsigned program, mode, port, expectedResets;
			};
			for (auto c : {Case{"held piano", 0, 0, 0, 0},
						   {"piano release", 0, 1, 0, 1},
						   {"organ release", 16, 1, 0, 2},
						   {"strings B", 48, 1, 1, 2},
						   {"sustain", 0, 2, 0, 1},
						   {"sostenuto", 16, 3, 0, 2},
						   {"volume zero", 16, 4, 0, 0},
						   {"expression zero", 16, 5, 0, 0},
						   {"velocity zero", 16, 6, 0, 2},
						   {"two ports", 0, 7, 0, 1}})
			{
				Sc88Pro b(roms.firmware, waves);
				std::unique_ptr<Sc88Pro> reference;
				if (c.program == 48)
				{
					reference = std::make_unique<Sc88Pro>(roms.firmware, waves);
					reference->m_autoVoiceReset = false;
				}
				Sc88Pro::SampleFrame referenceSample{};
				const auto render = [&]
				{
					const auto sample = b.renderSample();
					if (reference)
						referenceSample = reference->renderSample();
					return sample;
				};
				CHECK(b.m_autoVoiceReset);
				const auto send = [&](unsigned a, unsigned n, unsigned v, unsigned port)
				{
					const synthLib::SMidiEvent event(synthLib::MidiEventSource::Physical, a, n, v);
					b.addMidiEvent(event, port);
					if (reference)
						reference->addMidiEvent(event, port);
				};
				for (unsigned i = 0; i < 8 * 32000; ++i)
					render();
				send(0xc0, c.program, 0, c.port);
				for (unsigned i = 0; i < 32000; ++i)
					render();
				send(0x90, 60, 100, c.port);
				if (c.mode == 7)
					send(0x90, 64, 100, 1);
				uint64_t launched = 0, retired = 0;
				bool early = false;
				double heldEnergy = 0, referenceHeldEnergy = 0, heldDifferenceEnergy = 0, maxHeldDifference = 0;
				for (unsigned i = 0; i < 18 * 32000; ++i)
				{
					if (i == 32000 && (c.mode == 2 || c.mode == 3))
						send(0xb0, c.mode == 2 ? 64 : 66, 127, c.port);
					if (i == 2 * 32000)
					{
						if (c.expectedResets)
							send(c.mode == 6 ? 0x90 : 0x80, 60, 0, c.port);
						if (c.mode == 4 || c.mode == 5)
							send(0xb0, c.mode == 4 ? 7 : 11, 0, c.port);
					}
					if (i == 4 * 32000 && (c.mode == 2 || c.mode == 3))
						send(0xb0, c.mode == 2 ? 64 : 66, 0, c.port);
					const auto sample = render();
					if (reference && i < 2 * 32000)
					{
						heldEnergy += double(sample.first) * sample.first + double(sample.second) * sample.second;
						referenceHeldEnergy += double(referenceSample.first) * referenceSample.first +
							double(referenceSample.second) * referenceSample.second;
						const auto differenceLeft = double(sample.first) - referenceSample.first;
						const auto differenceRight = double(sample.second) - referenceSample.second;
						heldDifferenceEnergy += differenceLeft * differenceLeft + differenceRight * differenceRight;
						maxHeldDifference =
							std::max(maxHeldDifference, std::max(std::abs(differenceLeft), std::abs(differenceRight)));
					}
					if (i % 128)
						continue;
					const auto active = live(b);
					launched |= active;
					const auto newlyRetired = launched & ~active & ~retired;
					if (newlyRetired && i < (c.mode == 2 || c.mode == 3 ? 4u : 2u) * 32000)
						for (unsigned voice = 0; voice < 64; ++voice)
							if ((newlyRetired & (uint64_t{1} << voice))
								&& b.m_sram[b.m_voiceAllocation + voice * 2] != 0xff)
								early = true;
					retired |= newlyRetired;
				}
				unsigned count = 0;
				for (unsigned i = 0; i < 64; ++i)
					count += (retired >> i) & 1;
				CHECK(!early);
				CHECK_EQ(count, c.expectedResets);
				if (reference)
				{
					CHECK(referenceHeldEnergy > 0 && heldEnergy >= referenceHeldEnergy * 0.99);
					// Stopping a freed slot removes its residual fixed-point output.
					// Bound every sample to half a 16-bit output LSB, independently
					// of the held note's level (a relative-energy limit cannot do this).
					CHECK(maxHeldDifference <= xpLib::XP::outputFullScale / 65536.0);
					std::cout << "Strings held energy ratio=" << heldEnergy / referenceHeldEnergy
							  << ", relative difference=" << heldDifferenceEnergy / referenceHeldEnergy
							  << ", maximum sample difference=" << maxHeldDifference << std::endl;
				}
				std::cout << "Release test: " << c.name << " reset voices=" << count << std::endl;
				if (c.expectedResets)
				{
					// Exhaust the allocator so retired slots are reused; each new attack must render.
					for (unsigned n = 0; n < 70; ++n)
					{
						send(0x90, 60, 100, c.port);
						double energy = 0;
						for (unsigned i = 0; i < 3200; ++i)
						{
							auto s = render();
							energy += double(s.first) * s.first + double(s.second) * s.second;
						}
						CHECK(energy > 0);
						send(0x80, 60, 0, c.port);
						for (unsigned i = 0; i < 3200; ++i)
							render();
					}
				}
				// A completion followed by a new EG value before the 4 ms tick is cancelled.
				b.m_sram[b.m_releaseFlags] = 0x80;
				b.extWrite8(0xc00000 + b.m_releaseEg, 0);
				b.extWrite8(0xc00000 + b.m_releaseEg + 1, 0);
				CHECK(b.m_pendingVoiceResets & 1);
				b.extWrite8(0xc00000 + b.m_releaseEg, 0xff);
				CHECK(!(b.m_pendingVoiceResets & 1));
				b.m_pendingVoiceResets = 1;
				b.extWrite8(0xc83900, 0);
				b.extWrite8(0xc83901, 0);
				CHECK(!(b.m_pendingVoiceResets & 1));
				// A freed slot may be reallocated before the next 4 ms tick.
				b.extWrite8(0xc00000 + b.m_voiceAllocation, 0xff);
				CHECK(b.m_pendingVoiceResets & 1);
				b.extWrite8(0xc00000 + b.m_voiceAllocation, 2);
				CHECK(!(b.m_pendingVoiceResets & 1));
			}
		}
	};
} // namespace emu88Lib

void runSc88ReleaseTests(const char* path) { emu88Lib::Sc88ProReleaseTest::run(path); }
void runSc88VoiceReuseTests(const char* path)
{
	emu88Lib::Sc88ProReleaseTest::reuse(path, 8000);
	emu88Lib::Sc88ProReleaseTest::reuse(path, 32000);
}
