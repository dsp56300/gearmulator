#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <cmath>
#include <limits>

#include "dsp56kEmu/audio.h"
#include "jeLib/device.h"
#include "jeLib/je8086.h"
#include "jeLib/romloader.h"
#include "synthLib/wavWriter.h"
#include "baseLib/filesystem.h"

using namespace jeLib;

int main(int _argc, char* _argv[])
{
	std::cout << "JE-8086 Test Console\n";

	std::string romPath, stateDirectory, wavPath = "je8086_out.wav";
	double seconds = 0, demoSeconds = 0;	// --demo-seconds: stop the factory demo after that much audio
	unsigned programs = 0;
	unsigned demoSong = 0;
	unsigned demoLoops = 0;
	bool quiet = false;
	std::optional<esp::Engine> espEngine;
	const auto unsignedOption = [](const char* value)
	{
		const std::string text = value;
		size_t used = 0;
		const auto number = std::stoull(text, &used);
		if (text.empty() || text[0] == '-' || used != text.size() || number > std::numeric_limits<unsigned>::max())
			throw std::invalid_argument("Expected an unsigned integer: " + text);
		return static_cast<unsigned>(number);
	};
	try
	{
		for (int i = 1; i < _argc; ++i)
		{
			const std::string arg = _argv[i];
			if (arg == "--help")
			{
				std::cout << "--rom FILE --state-directory DIR --wav FILE --quiet\n"
					"--demo-seconds N   Limit factory demo audio (0 = unlimited)\n"
					"--demo-song N      Select 1-8 (JP-8000) or 1-16 (JP-8080); 0 = consecutive playback\n"
					"--demo-loops N     Stop after N complete demo cycles (requires song 0)\n"
					"--esp-engine jit|interpreter\n"
					"--seconds N --programs N   Render the MIDI note/program test instead\n";
				return 0;
			}
			if (arg == "--quiet") { quiet = true; continue; }
			if (i + 1 == _argc) { std::cerr << "Missing option value\n"; return 1; }
			if (arg == "--rom") romPath = _argv[++i];
			else if (arg == "--seconds") seconds = std::stod(_argv[++i]);
			else if (arg == "--programs") programs = unsignedOption(_argv[++i]);
			else if (arg == "--state-directory") stateDirectory = _argv[++i];
			else if (arg == "--wav") wavPath = _argv[++i];
			else if (arg == "--demo-seconds") demoSeconds = std::stod(_argv[++i]);
			else if (arg == "--demo-song") demoSong = unsignedOption(_argv[++i]);
			else if (arg == "--demo-loops") demoLoops = unsignedOption(_argv[++i]);
			else if (arg == "--esp-engine")
			{
				// Select the native whole-core engine or the canonical interpreter.
				const std::string engine = _argv[++i];
				if (engine == "interpreter") espEngine = esp::Engine::Interpreter;
				else if (engine == "jit") espEngine = esp::Engine::Jit;
				else { std::cerr << "Unknown ESP engine: " << engine << '\n'; return 1; }
			}
			else { std::cerr << "Unknown option: " << arg << '\n'; return 1; }
		}
	}
	catch (const std::exception& e) { std::cerr << "Invalid option: " << e.what() << '\n'; return 1; }
	if (programs > 128 || !std::isfinite(seconds) || !std::isfinite(demoSeconds) || seconds < 0 || demoSeconds < 0) return 1;
	if (demoLoops && demoSong) { std::cerr << "Demo loops require consecutive playback (--demo-song 0)\n"; return 1; }
	std::cout << "Searching for ROM...\n";
	auto rom = romPath.empty() ? RomLoader::findROM() : RomLoader::loadROM(romPath);

	if (!rom.isValid())
	{
		std::cerr << "No valid ROM found.\n";
		return -1;
	}

	std::cout << "ROM found, creating device...\n";

	synthLib::DeviceCreateParams params;

	params.romData = rom.getData();
	params.romName = rom.getName();
	params.homePath = stateDirectory;
	// the factory-reset SRAM image is stored here
	if (!stateDirectory.empty()) baseLib::filesystem::createDirectory(stateDirectory + "/roms");

	constexpr uint32_t samplerate = 88200;
	params.hostSamplerate = samplerate;
	params.preferredSamplerate = samplerate;

	try
	{
		Device device(params);
		const bool rack = device.getJe8086().isRack();
		if (demoSong > (rack ? 16u : 8u)) { std::cerr << "Invalid demo song for this model\n"; return 1; }
		if (espEngine) device.getJe8086().setEspEngine(*espEngine);
		const auto printEspStatistics = [&device]
		{
			size_t index = 0;
			device.getJe8086().forEachEsp([&index](const auto& _asic)
			{
				const auto& s = _asic.statistics();
				std::cout << "ESP " << index++ << ": frames interpreted=" << s.interpreted << " native=" << s.native
					<< " compiles=" << s.compiles << " adopted=" << s.adopted << " failed=" << s.failed << '\n';
			});
		};

		device.setMasterVolume(7.0f);

		std::cout << "Boot done, starting factory demo playback...\n";

		// prepare audio buffers
		std::array<std::vector<float>, 2> outBuffers;

		synthLib::TAudioInputs inputs{};
		synthLib::TAudioOutputs outputs{};

		constexpr size_t blocksize = 128;

		for (size_t i=0; i<outBuffers.size(); ++i)
		{
			outBuffers[i].resize(blocksize);
			outputs[i] = outBuffers[i].data();
		}

		std::vector<synthLib::SMidiEvent> midiIn;
		std::vector<synthLib::SMidiEvent> midiOut;

		if (seconds > 0)
		{
			synthLib::AsyncWriter writer(wavPath, samplerate);
			uint64_t total = 0;
			const uint64_t startCycles = device.getJe8086().cycles();
			double energy = 0, peak = 0;
			int lastSecond = -1;
			while (total < uint64_t(seconds * samplerate))
			{
				const int second = int(total / samplerate);
				if (second != lastSecond)
				{
					lastSecond = second;
					// Allow firmware/UI startup, then alternate note and release.
					if (second >= 10)
					{
						auto add = [&](uint8_t status, uint8_t a, uint8_t b)
						{
							synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
							event.a = status; event.b = a; event.c = b;
							midiIn.push_back(event);
						};
						if ((second & 1) == 0)
						{
							const unsigned program = unsigned(second - 10) / 2;
							if (program < programs) add(0xc0, uint8_t(program), 0);
							add(0x90, 60, 100);
						}
						else add(0x80, 60, 0);
					}
					std::cout << "Rendered second " << second << std::endl;
				}
				device.process(inputs, outputs, blocksize, midiIn, midiOut);
				midiIn.clear(); midiOut.clear();
				for (const auto& buffer : outBuffers)
					for (float sample : buffer)
					{
						if (!std::isfinite(sample)) return 2;
						energy += double(sample) * sample;
						peak = std::max(peak, std::abs(double(sample)));
					}
				writer.append([&outBuffers](std::vector<dsp56k::TWord>& dst)
				{
					for (size_t i = 0; i < blocksize; ++i)
						for (const auto& buffer : outBuffers) dst.push_back(dsp56k::sample2dsp(buffer[i]));
				});
				total += blocksize;
			}
			std::cout << "Audio peak=" << peak << " RMS=" << std::sqrt(energy / (2 * total)) << '\n';
			printEspStatistics();
			const double boardSeconds = double(device.getJe8086().cycles() - startCycles) / Je8086::McuClock;
			const double audioSeconds = double(total) / samplerate;
			std::cout << "Cadence board=" << boardSeconds << "s audio=" << audioSeconds << "s\n";
			if (std::abs(boardSeconds - audioSeconds) > 0.01)
			{
				std::cerr << "FAIL: board and host audio clocks diverge\n";
				return 3;
			}
			return 0;
		}

		uint64_t sampleCounter = 0;

		bool bootFinished = false;
		bool demoRunning = false;
		unsigned currentSong = 0, completedLoops = 0;

		SysexRemoteControl sysexRemote;

		sysexRemote.evLcdDdDataChanged.addListener([&](const std::array<char, 40>& _lcdContent)
		{
			char lcdString[41]{0};

			for (size_t i=0; i<_lcdContent.size(); ++i)
				lcdString[i] = _lcdContent[i] >= ' ' ? static_cast<char>(_lcdContent[i]) : ' ';

			std::string s(lcdString);
			// Wait for the title, not intermediate character writes of the LCD.
			const std::string title = s.substr(20);
			const auto colon = title.find(':');
			if (demoRunning && colon > 0 && colon <= 2 && title[0] >= '0' && title[0] <= '9')
			{
				const unsigned song = unsigned(std::strtoul(title.c_str(), nullptr, 10));
				if (song >= 1 && song <= (rack ? 16u : 8u) && song != currentSong)
				{
					if (song == 1 && currentSong == (rack ? 16u : 8u)) ++completedLoops;
					currentSong = song;
					std::cout << "Demo song: " << song << std::endl;
				}
			}

			if (!quiet)
			{
				std::cout << "LCD: [" << s.substr(0 , rack ? 20 : 16) << "]\n";
				std::cout << "LCD: [" << s.substr(20, rack ? 20 : 16) << "]\n";
			}

			if (!bootFinished)
			{
				if (s.find("PERFORM") != std::string::npos)
				{
					bootFinished = true;
					std::cout << "Boot finished, starting demo playback...\n";

					if (rack)
					{
						device.getJe8086().setButton(je8080::kSwitch_Shift, true);
						device.getJe8086().setButton(je8080::kSwitch_Exit, true);
					}
					else
					{
						device.getJe8086().setButton(devices::kSwitch_Rec, true);
						device.getJe8086().setButton(devices::kSwitch_Hold, true);
					}
				}
			}
			else if(!demoRunning)
			{
				if (s.find(rack ? "DEMO" : "=== ROM PLAY ===") != std::string::npos)
				{
					demoRunning = true;
					std::cout << "Demo playback started, starting recording to .wav file.\n";
				}
			}
		});


		uint64_t startupSamples = 0;
		while (!demoRunning)
		{
			device.process(inputs, outputs, blocksize, midiIn, midiOut);

			for (const auto& e : midiOut)
				sysexRemote.receive(e);
			midiOut.clear();
			startupSamples += blocksize;
			if (startupSamples > samplerate * 30)
				throw std::runtime_error("Firmware did not enter demo playback within 30 seconds");
		}

		if (rack)
		{
			device.getJe8086().setButton(je8080::kSwitch_Shift, false);
			device.getJe8086().setButton(je8080::kSwitch_Exit, false);
		}
		if (demoSong)
		{
			if (!rack)
			{
				device.getJe8086().setButton(devices::kSwitch_Rec, false);
				device.getJe8086().setButton(devices::kSwitch_Hold, false);
			}
			const auto settle = [&]
			{
				for (unsigned n = 0; n < samplerate / 5; n += blocksize)
				{
					device.process(inputs, outputs, blocksize, midiIn, midiOut);
					for (const auto& e : midiOut) sysexRemote.receive(e);
					midiOut.clear();
				}
			};
			settle();
			constexpr devices::SwitchType keys[] = {devices::kSwitch_1, devices::kSwitch_2, devices::kSwitch_3,
				devices::kSwitch_4, devices::kSwitch_5, devices::kSwitch_6, devices::kSwitch_7, devices::kSwitch_8};
			const auto key = static_cast<je8080::SwitchType>(je8080::kSwitch_Bank1 + demoSong - 1);
			if (rack) device.getJe8086().setButton(key, true);
			else device.getJe8086().setButton(keys[demoSong - 1], true);
			settle();
			if (rack) device.getJe8086().setButton(key, false);
			else device.getJe8086().setButton(keys[demoSong - 1], false);
		}

		synthLib::AsyncWriter writer(wavPath, samplerate);

		auto t0 = std::chrono::high_resolution_clock::now();
		auto tLast = t0;
		uint64_t totalProcessedSamples = 0;
		uint32_t intervalProcessedSamples = 0;

		while (true)
		{
			device.process(inputs, outputs, blocksize, midiIn, midiOut);

			for (const auto& e : midiOut)
				sysexRemote.receive(e);
			midiOut.clear();

			sampleCounter += blocksize;

			writer.append([&outBuffers, blocksize](std::vector<dsp56k::TWord>& _dst)
			{
				_dst.reserve(_dst.size() + blocksize * 2);

				for (size_t i=0; i<blocksize; ++i)
				{
					_dst.push_back(dsp56k::sample2dsp(outBuffers[0][i]));
					_dst.push_back(dsp56k::sample2dsp(outBuffers[1][i]));
				}
			});

			totalProcessedSamples += blocksize;
			intervalProcessedSamples += blocksize;
			if ((demoSeconds > 0 && double(totalProcessedSamples) >= demoSeconds * samplerate)
				|| (demoLoops > 0 && completedLoops >= demoLoops))
			{
				const std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - t0;
				std::cout << "Demo: " << totalProcessedSamples / samplerate << " s of audio in " << elapsed.count() << " s, "
					<< int(100.0 * double(totalProcessedSamples) / (elapsed.count() * samplerate)) << "% of real time\n";
				printEspStatistics();
				if (demoLoops > 0 && completedLoops < demoLoops)
					throw std::runtime_error("Demo time limit reached before the requested loops completed");
				break;
			}

			if(!quiet && intervalProcessedSamples >= samplerate)
			{
				auto tNow = std::chrono::high_resolution_clock::now();
				std::chrono::duration<double> elapsed = tNow - tLast;
				tLast = tNow;
				double intervalRealTimePercent = 100.0f * static_cast<double>(intervalProcessedSamples) / (elapsed.count() * params.hostSamplerate);
				double realTimePercent = 100.0f * static_cast<double>(totalProcessedSamples) / (std::chrono::duration<double>(tNow - t0).count() * params.hostSamplerate);
				std::cout << "Recorded " << (totalProcessedSamples / samplerate) << " sec"
						  << ", last interval speed " << static_cast<int>(intervalRealTimePercent) << "%, "
						  << "total average " << static_cast<int>(realTimePercent) << "%\n";
				intervalProcessedSamples -= samplerate;
			}
		}
	}
	catch (std::exception& e)
	{
		std::cerr << "Error: " << e.what() << "\n";
		return -1;
	}

	return 0;
}
