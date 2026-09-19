#include "88lib/hardwareDevice.h"
#include "88lib/boards/sc88pro.h"
#include "88lib/rom/romloader.h"
#include "common/test_util.hpp"

#include <algorithm>
#include <chrono>
#include <tuple>
#include "baseLib/filesystem.h"
#include "synthLib/plugin.h"
#include "baseLib/os.h"
#ifdef TEST_JUCE_INPUT
#include "jucePlayerLib/midiInputRouting.h"
#include "jucePlayerLib/midiPlayer.h"
#endif

int main()
{
	baseLib::disableErrorDialogs();

	using namespace emu88Lib;
	using namespace synthLib;
	// SC-88Pro switch-board wiring: common supply, eight cathodes and a separate red cathode.
	{
		struct PanelBoard : Sc88Pro
		{
			PanelBoard() : Sc88Pro(std::vector<uint8_t>(RomSize), {}, false) {}
			using Sc88Pro::extRead8;
			using Sc88Pro::extWrite8;
		};
		PanelBoard panel;
		panel.extWrite8(0xefc101, 0xfc);
		for(unsigned bit = 0; bit < 8; ++bit)
		{
			panel.extWrite8(0xefc100, static_cast<uint8_t>(1u << bit));
			CHECK_EQ(panel.leds(), 1u << bit);
		}
		panel.extWrite8(0xefc101, 0xfe);
		CHECK_EQ(panel.leds(), 0x180u); // Both dies: orange.
		panel.extWrite8(0xefc100, 0);
		CHECK_EQ(panel.leds(), 0x100u); // Red without green.
		panel.extWrite8(0xefc100, 0xff);
		panel.extWrite8(0xefc101, 0xff);
		CHECK_EQ(panel.leds(), 0u); // Common supply disabled.
		CHECK_EQ(panel.extRead8(0xefc100), 0xffu);
		CHECK_EQ(panel.extRead8(0xefc101), 0xffu);
		panel.extWrite8(0xefc101, 0xfc);
		CHECK_EQ(panel.leds(), 0xffu); // Data latch survives blanking.

		// POWER is a mains switch, not a key at matrix position 0. Even a raw panel
		// command must leave that input open, while the adjacent SC-88 MAP key works.
		panel.extWrite8(0xe000fe, 1); // Scan the first column.
		panel.setButtons(1u << static_cast<uint8_t>(Sc88ProButton::Power));
		CHECK_EQ(panel.extRead8(0xe000fe), 0xffu);
		panel.setButtons((1u << static_cast<uint8_t>(Sc88ProButton::Power)) |
		                 (1u << static_cast<uint8_t>(Sc88ProButton::Sc88Map)));
		CHECK_EQ(panel.extRead8(0xe000fe), 0xfdu);
		panel.setButtons(0);
		CHECK_EQ(panel.extRead8(0xe000fe), 0xffu);
	}
	// Retain P6DR behavior independently of the physical POWER switch classification.
	{
		struct DisplayBoard : Sc88Pro
		{
			DisplayBoard() : Sc88Pro(std::vector<uint8_t>(RomSize), {}, false) {}
			using Sc88Pro::portWrite;
		};
		DisplayBoard display;
		CHECK(display.lcdEnabled());
		display.portWrite(0xfe8b, 0xfe);
		CHECK(!display.lcdEnabled());
		display.portWrite(0xfe8a, 0x01); // P5DR is not the line.
		CHECK(!display.lcdEnabled());
		display.portWrite(0xfe8b, 0x01);
		CHECK(display.lcdEnabled());
	}
	static_assert(getPowerSwitch(DeviceModel::Sc55Mk1) == PowerSwitch::Standby);
	static_assert(getPowerSwitch(DeviceModel::Sc55Mk2) == PowerSwitch::Standby);
	static_assert(getPowerSwitch(DeviceModel::Sc155) == PowerSwitch::Standby);
	static_assert(getPowerSwitch(DeviceModel::Sc155Mk2) == PowerSwitch::Standby);
	static_assert(getPowerSwitch(DeviceModel::Sc88VL) == PowerSwitch::Standby);
	static_assert(getPowerSwitch(DeviceModel::Sc55St) == PowerSwitch::Supply);
	static_assert(getPowerSwitch(DeviceModel::Sc88) == PowerSwitch::Supply);
	static_assert(getPowerSwitch(DeviceModel::Sc88Pro) == PowerSwitch::Supply);
	static_assert(getPowerSwitch(DeviceModel::Sc8820) == PowerSwitch::Supply);
	static_assert(getPowerSwitch(DeviceModel::Sc8850) == PowerSwitch::Supply);
	static_assert(getPowerSwitch(DeviceModel::VeGsPro) == PowerSwitch::Supply);

	namespace fs = baseLib::filesystem;
	const auto folder = fs::getCurrentDirectory() + "88emu-transport-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "/";
	CHECK(fs::createDirectory(folder.substr(0, folder.size() - 1)));
	struct Cleanup
	{
		std::string folder;
		~Cleanup()
		{
			for(const auto* name : {"cm32p_program.bin", "cm32p_wave0.bin", "cm32p_wave1.bin", "cm32p_wave2.bin"})
				fs::remove(folder + name);
			fs::remove(folder);
		}
	} cleanup{folder};
	const auto write = [&](const char* name, const std::vector<uint8_t>& bytes)
	{
		CHECK(fs::writeFile(folder + name, bytes));
	};
	std::vector<uint8_t> program(Cm32pRomSet::ProgramSize, 0xff);
	// Fast UART, poll RX-ready and echo each received byte. No copyrighted ROMs.
	const uint8_t echo[]{0xb1, 0, 0x0e, 0xb1, 0x80, 0x0e,
		0xb0, 0x11, 0x20, 0x36, 0x20, 0xfa, 0xb0, 7, 7, 0x27, 0xf5};
	std::copy(std::begin(echo), std::end(echo), program.begin() + 0x2080);
	write("cm32p_program.bin", program);
	const std::vector<uint8_t> wave(Cm32pRomSet::WaveSize);
	for(const auto* name : {"cm32p_wave0.bin", "cm32p_wave1.bin", "cm32p_wave2.bin"}) write(name, wave);
	synthLib::RomLoader::setSearchPath(folder);
	emu88Lib::RomLoader::rescan();
	DeviceCreateParams params;
	params.customData = static_cast<uint32_t>(DeviceModel::Cm32p);
	HardwareDevice device(params);
	CHECK(device.isValid());
	std::array<float, 512> left{}, right{};
	TAudioOutputs audio{};
	audio[0] = left.data(); audio[1] = right.data();
	std::vector<SMidiEvent> output;
	const auto process = [&](size_t frames, const std::vector<SMidiEvent>& input)
	{
		device.process({}, audio, frames, input, output);
	};
	SMidiEvent note(MidiEventSource::Host, 0x90, 60, 100, 16);
	SMidiEvent future(MidiEventSource::Host, 0x90, 61, 100, 800);
	process(16, {note, future});
	CHECK(output.empty()); // Event at the boundary belongs to the next block.
	process(128, {});
	CHECK_EQ(output.size(), 1u);
	if(output.size() == 1) CHECK_EQ(output[0].b, 60);

	SMidiEvent jump(MidiEventSource::Internal);
	jump.type = MidiEventType::TransportDiscontinuity;
	jump.transportGeneration = 1;
	SMidiEvent stale(MidiEventSource::Host, 0x90, 62, 100);
	SMidiEvent physical(MidiEventSource::Physical, 0x90, 63, 100);
	SMidiEvent current(MidiEventSource::Host, 0x90, 64, 100);
	current.transportGeneration = 1;
	process(512, {jump, stale, physical, current});
	// The echo preserves running status. Reassemble the parser's raw data-byte
	// events; both the board limiter and transport cleanup emit All Sound Off.
	std::vector<uint8_t> bytes;
	for(const auto& event : output)
	{
		bytes.push_back(event.a);
		const auto length = MidiBufferParser::lengthFromStatusByte(event.a);
		if(length > 1) bytes.push_back(event.b);
		if(length > 2) bytes.push_back(event.c);
	}
	const std::vector<uint8_t> expected{0xb0, 120, 0, 120, 0, 0x90, 63, 100, 64, 100};
	CHECK(bytes == expected);
	process(512, {});
	CHECK(output.empty()); // The previously queued future note is obsolete too.
	CHECK_EQ(device.getExtraLatencySamples(), 0u);
	CHECK(device.displaySnapshot().revision > 0);

	// All Notes Off can leave sustain/release voices sounding when transport jumps.
	{
		HardwareDevice board(params, BootOptions{false, false});
		board.process({}, {}, 512, {
			SMidiEvent(MidiEventSource::Host, 0x90, 60, 100),
			SMidiEvent(MidiEventSource::Host, 0xb0, 64, 127, 64),
			SMidiEvent(MidiEventSource::Host, 0xb0, MC_ALLNOTESOFF, 0, 128)}, output);
		board.process({}, {}, 512, {jump}, output);
		bytes.clear();
		for(const auto& event : output)
		{
			bytes.push_back(event.a);
			const auto length = MidiBufferParser::lengthFromStatusByte(event.a);
			if(length > 1) bytes.push_back(event.b);
			if(length > 2) bytes.push_back(event.c);
		}
		// Both the board's UART queue and HardwareDevice retain their cleanup.
		CHECK(bytes == std::vector<uint8_t>({0xb0, 120, 0, 120, 0}));
		board.process({}, {}, 512, {jump}, output);
		CHECK(output.empty());
	}

	// UART reply timestamps must be independent of callback boundaries and use output samples.
	using TimedReply = std::tuple<uint64_t, uint8_t, uint8_t, uint8_t, uint8_t>;
	const auto timedReplies = [&](uint32_t oversampling, const std::vector<size_t>& blocks)
	{
		HardwareDevice board(params, BootOptions{false, false});
		if(oversampling != 1)
		{
			board.setAnalogOutputMode(AnalogOutputMode::Auto);
			CHECK(board.setSamplerate(32000.0f * oversampling));
		}
		std::vector<SMidiEvent> input{
			SMidiEvent(MidiEventSource::Physical, 0x90, 60, 100, 32 * oversampling),
			SMidiEvent(MidiEventSource::Physical, 0x91, 61, 100, 160 * oversampling),
			SMidiEvent(MidiEventSource::Physical, 0x92, 62, 100, 420 * oversampling)};
		std::vector<TimedReply> replies;
		uint64_t position = 0;
		for(const auto count : blocks)
		{
			board.process({}, {}, count, input, output);
			input.clear();
			for(const auto& event : output)
			{
				CHECK(event.offset < count);
				replies.emplace_back(position + event.offset, event.a, event.b, event.c, event.port);
			}
			position += count;
		}
		return replies;
	};
	const auto nativeReplies = timedReplies(1, {1024});
	CHECK_EQ(nativeReplies.size(), 3u);
	CHECK(nativeReplies == timedReplies(1, {0, 1, 31, 0, 129, 351, 512}));
	const auto oversampledReplies = timedReplies(8, {8192});
	CHECK(oversampledReplies == timedReplies(8, {0, 1, 31, 0, 129, 351, 7680}));
	auto scaledReplies = nativeReplies;
	for(auto& reply : scaledReplies) std::get<0>(reply) *= 8;
	CHECK(oversampledReplies == scaledReplies);
	if(nativeReplies.size() == 3)
	{
		CHECK(std::get<0>(nativeReplies[0]) >= 32);
		CHECK(std::get<0>(nativeReplies[1]) >= 160);
		CHECK(std::get<0>(nativeReplies[2]) >= 420);
	}

	// An analogue model that oversamples changes the device rate only once the engine asks for it.
	HardwareDevice analog(params);
	CHECK(analog.isValid());
	analog.setAnalogOutputMode(AnalogOutputMode::Auto);
	CHECK_EQ(analog.getSamplerate(), 32000.0f);
	Plugin engine(&analog, [](Device*) { return nullptr; });
	engine.setHostSamplerate(48000.0f, 0.0f);
	CHECK_EQ(analog.getSamplerate(), 256000.0f);
	CHECK(analog.analogModel() == AnalogModel::Cm32p);
	// Offsets stay in output samples: an event at the boundary still belongs to the next block,
	// which starts with a new DAC frame.
	analog.process({}, audio, 64, {SMidiEvent(MidiEventSource::Host, 0x90, 60, 100, 64)}, output);
	CHECK(output.empty());
	analog.process({}, audio, 512, {}, output);
	CHECK_EQ(output.size(), 1u);
	CHECK(std::all_of(left.begin(), left.end(), [](const float _sample) { return _sample == 0.0f; }));
	analog.setAnalogOutputMode(AnalogOutputMode::Off);
	CHECK_EQ(analog.getSamplerate(), 256000.0f);
	CHECK(engine.setPreferredDeviceSamplerate(0.0f));
	CHECK_EQ(analog.getSamplerate(), 32000.0f);
	CHECK(analog.analogModel() == AnalogModel::None);

#ifdef TEST_JUCE_INPUT
	// Live input is queued before the file player's discontinuity in the same block.
	const auto notesAfterStop = [&](MidiEventSource source)
	{
		HardwareDevice board(params, BootOptions{false, false});
		Plugin plugin(&board, [](Device*) { return nullptr; });
		plugin.setMidiClockEnabled(false);
		plugin.setResamplerMode(Resampler::Mode::Legacy);
		plugin.setHostSamplerate(32000.0f, 0);
		plugin.setBlockSize(512);
		plugin.setLatencyBlocks(0);
		juce::MidiBuffer live;
		live.addEvent(juce::MidiMessage::noteOn(1, 70, uint8_t{100}), 128);
		jucePlayer::forEachMidiEvent(live, 0, source,
			[&](SMidiEvent event) { plugin.addMidiEvent(event); });
		jucePlayer::MidiPlayer player;
		player.setPortCount(1);
		player.stop();
		std::vector<SMidiEvent> transport;
		player.processBlock(transport, 512, 32000);
		for(const auto& event : transport) plugin.addMidiEvent(event);
		unsigned notes = 0;
		for(unsigned block = 0; block < 8; ++block)
		{
			plugin.process({}, audio, 512, 120, 0, false, false);
			std::vector<SMidiEvent> reply;
			plugin.getMidiOut(reply);
			for(const auto& event : reply)
				if(event.a == 0x90 && event.b == 70) ++notes;
		}
		return notes;
	};
	CHECK_EQ(notesAfterStop(MidiEventSource::Physical), 1u);
	CHECK_EQ(notesAfterStop(MidiEventSource::Host), 0u);
#endif
	return test::finish("hardware device transport");
}
