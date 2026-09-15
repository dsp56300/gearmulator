#include "sc88Renderer.h"
#include "cpu/common/test_util.hpp"
#include "hardwareDevice.h"
#include "synthLib/plugin.h"
#include "synthLib/romLoader.h"

#include <cmath>
#include <iostream>
#include <thread>

using namespace emu88Lib;
using namespace synthLib;
using namespace test;

int main(int argc, char** argv)
{
	const auto caller = std::this_thread::get_id();
	int32_t frames = 0;
	std::vector<std::pair<int32_t, SMidiEvent>> received;
	Sc88Renderer renderer(
		[&]
		{
			CHECK(std::this_thread::get_id() == caller);
			const auto n = ++frames;
			return Sc88Renderer::SampleFrame{n, -n};
		},
		[&](const SMidiEvent& e) { received.emplace_back(frames, e); },
		[&](auto& events)
		{
			if (frames == 4)
				events.emplace_back(MidiEventSource::Device, 0x90, 60, 100);
		},
		[&] { CHECK(std::this_thread::get_id() == caller); });
	std::vector<SMidiEvent> input, output;
	float left[48]{}, right[48]{};
	input.emplace_back(MidiEventSource::Host, 0x90, 60, 100, 3);
	input.emplace_back(MidiEventSource::Host, 0x90, 61, 100, 40);
	input.back().port = 1;
	renderer.processSamples(0, nullptr, nullptr, 0.5f, input, output);
	CHECK_EQ(frames, 0);
	renderer.processSamples(16, left, right, 0.5f, input, output);
	CHECK_EQ(frames, 16); // all work is finished before returning
	CHECK_EQ(left[0], 0.5f); // no initial queue silence or block of latency
	CHECK_EQ(right[15], -8.0f);
	CHECK_EQ(received.size(), 1u);
	CHECK_EQ(received[0].first, 3);
	CHECK_EQ(output.size(), 1u);
	CHECK_EQ(output[0].offset, 3u);

	SMidiEvent marker(MidiEventSource::Internal);
	marker.type = MidiEventType::TransportDiscontinuity;
	marker.transportGeneration = 1;
	input.push_back(marker);
	input.emplace_back(MidiEventSource::Host, 0x90, 62, 100);
	input.emplace_back(MidiEventSource::Physical, 0x90, 63, 100);
	input.emplace_back(MidiEventSource::Host, 0x90, 64, 100);
	input.back().transportGeneration = 1;
	input.back().port = 1;
	renderer.processSamples(48, left, right, 0.5f, input, output);
	CHECK_EQ(frames, 64);
	CHECK_EQ(left[0], 8.5f);
	CHECK_EQ(received.size(), 4u);
	CHECK_EQ(received[1].second.b, MC_ALLSOUNDOFF);
	CHECK_EQ(received[2].second.b, 63);
	CHECK_EQ(received[3].second.b, 64);
	CHECK_EQ(received[3].second.port, 1);
	CHECK_EQ(received[3].first, 16);

	// A future port-B event must retain its offset across arbitrary partitions.
	input.emplace_back(MidiEventSource::Physical, 0x91, 65, 100, 9);
	input.back().port = 1;
	for (const auto count : {1u, 3u, 0u, 7u, 32769u})
	{
		std::vector<float> l(count + 1, -999), r(count + 1, -999);
		const auto start = frames;
		renderer.processSamples(count, l.data(), r.data(), 0.5f, input, output);
		CHECK_EQ(frames, start + static_cast<int32_t>(count));
		for (uint32_t i = 0; i < count; ++i)
		{
			CHECK_EQ(l[i], (start + i + 1) * 0.5f);
			CHECK_EQ(r[i], -l[i]);
		}
		CHECK_EQ(l[count], -999.0f);
		CHECK_EQ(r[count], -999.0f);
	}
	CHECK_EQ(received.size(), 5u);
	CHECK_EQ(received.back().first, 73);
	CHECK_EQ(received.back().second.port, 1);

	{
		std::vector<SMidiEvent> sent, pending, replies;
		Sc88Renderer transport([] { return Sc88Renderer::SampleFrame{}; },
							   [&](const SMidiEvent& event) { sent.push_back(event); }, {}, {});
		pending.emplace_back(MidiEventSource::Host, 0x90, 60, 100);
		pending.emplace_back(MidiEventSource::Host, 0xb0, MC_ALLNOTESOFF, 0);
		transport.processSamples(1, nullptr, nullptr, 1.0f, pending, replies);
		SMidiEvent sysex(MidiEventSource::Host);
		sysex.sysex = {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};
		sysex.offset = 2;
		pending.push_back(sysex);
		pending.push_back(marker);
		transport.processSamples(3, nullptr, nullptr, 1.0f, pending, replies);
		CHECK_EQ(sent.size(), 4u);
		CHECK_EQ(sent[2].b, MC_ALLSOUNDOFF);
		CHECK(sent[3].sysex == sysex.sysex);
		pending.push_back(marker);
		transport.processSamples(1, nullptr, nullptr, 1.0f, pending, replies);
		CHECK_EQ(sent.size(), 4u);
	}
	// Optional local ROM check; the portable CTest run does not require ROMs.
	if (argc == 2)
	{
		RomLoader::addSearchPath(argv[1], true);
		DeviceCreateParams params;
		params.customData = static_cast<uint32_t>(DeviceModel::Sc88Pro);
		HardwareDevice device(params);
		CHECK(device.isValid());
		if (!device.isValid())
			return finish("sc88Renderer ROM missing");
		Plugin engine(&device, [](Device*) { return nullptr; });
		engine.setMidiClockEnabled(false);
		engine.setLatencyBlocks(0);
		engine.setBlockSize(256);
		for (const auto rate : {32000.0f, 44100.0f, 48000.0f})
		{
			engine.setHostSamplerate(rate, 0.0f);
			std::vector<float> l(1024), r(1024);
			TAudioInputs in{};
			TAudioOutputs out{l.data(), r.data()};
			// Match the board's eight-second firmware boot allowance.
			for (int i = 0; i < rate * 8 / 256; ++i)
				engine.process(in, out, 256, 120, 0, false, false);
			for (uint8_t port = 0; port < 2; ++port)
			{
				SMidiEvent note(MidiEventSource::Physical, 0x90, 60, 100, 17);
				note.port = port;
				engine.addMidiEvent(note);
				double energy = 0;
				for (int i = 0; i < 160; ++i)
				{
					const auto count = i % 2 ? 257u : 127u;
					engine.process(in, out, count, 120, 0, false, false);
					for (size_t j = 0; j < count; ++j)
					{
						CHECK(std::isfinite(l[j]) && std::isfinite(r[j]));
						energy += l[j] * l[j] + r[j] * r[j];
					}
				}
				std::cout << "ROM rate=" << rate << " port=" << unsigned(port) << " energy=" << energy << std::endl;
				CHECK(energy > 0.0001);
				note.a = 0xb0;
				note.b = MC_ALLSOUNDOFF;
				note.c = 0;
				engine.addMidiEvent(note);
				for (int i = 0; i < rate * 2 / 256; ++i)
					engine.process(in, out, 256, 120, 0, false, false);
			}
		}
	}
	return finish("sc88Renderer");
}
