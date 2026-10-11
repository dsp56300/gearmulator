// The host's transport start/stop/seek marker is not MIDI - it carries no status
// or data bytes. Device::process must hand it to onTransportDiscontinuity, never
// to sendMidi: devices push an event's bytes straight into their emulated UART,
// and a lone 0x00 there completes the firmware's running-status message, which
// on at least one device turned every "play" into a Program Change.
#include "consoleTools.h"
#include "device.h"
#include "plugin.h"
#include "baseLib/os.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace
{
	using namespace synthLib;

	class TestDevice : public Device
	{
	public:
		TestDevice() : Device(DeviceCreateParams()) {}

		std::vector<SMidiEvent> m_midiSent;
		std::vector<SMidiEvent> m_transportEvents;

		float getSamplerate() const override { return 44100.0f; }
		bool isValid() const override { return true; }
		bool getState(std::vector<uint8_t>&, StateType) override { return false; }
		bool setState(const std::vector<uint8_t>&, StateType) override { return false; }
		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return 2; }

	protected:
		void readMidiOut(std::vector<SMidiEvent>&) override {}
		void processAudio(const TAudioInputs&, const TAudioOutputs&, size_t) override {}
		bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>&) override
		{
			m_midiSent.push_back(_ev);
			return true;
		}
		void onTransportDiscontinuity(const SMidiEvent& _ev) override
		{
			m_transportEvents.push_back(_ev);
		}
	};

	// Has an audio input and a second stereo output. Reads the input, then writes every output.
	class FeedbackProbe final : public TestDevice
	{
	public:
		bool m_heardOutput = false;

		uint32_t getChannelCountIn() override { return 2; }
		uint32_t getChannelCountOut() override { return 4; }

	protected:
		void processAudio(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, const size_t _size) override
		{
			for (size_t i = 0; i < _size; ++i)
			{
				if (_inputs[0][i] != 0.0f || _inputs[1][i] != 0.0f)
					m_heardOutput = true;
			}

			for (size_t c = 0; c < 4; ++c)
				std::fill_n(_outputs[c], _size, 1.0f);
		}
	};

	// Stamps every event the way a device with a sample scheduling firmware does
	class StampingDevice final : public TestDevice
	{
	protected:
		bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>&) override
		{
			auto e = _ev;
			stampOffset(e);
			m_midiSent.push_back(e);
			return true;
		}
	};

	// Sends its LCD as a front panel frame after every block
	class PanelDevice final : public TestDevice
	{
	protected:
		void readMidiOut(std::vector<SMidiEvent>& _midiOut) override
		{
			const uint8_t cells[] = {'H', 'i'};
			fp::sendLcdText(_midiOut, 2, 1, cells);
		}
	};

	void expect(const bool _condition)
	{
		if (!_condition)
			std::abort();
	}

	void testTransportMarkerBypassesSendMidi()
	{
		TestDevice device;

		SMidiEvent marker(MidiEventSource::Internal);
		marker.type = MidiEventType::TransportDiscontinuity;
		marker.transportGeneration = 7;

		const SMidiEvent note(MidiEventSource::Host, 0x90, 0x40, 0x7f);

		std::vector<SMidiEvent> midiOut;
		float buffer[8] = {};
		float* const ptr = buffer;
		const TAudioInputs in = {ptr, ptr, nullptr, nullptr};
		const TAudioOutputs out = {ptr, ptr};

		device.process(in, out, 8, {marker, note}, midiOut);

		// The marker reaches the transport hook and nothing else...
		expect(device.m_transportEvents.size() == 1);
		expect(device.m_transportEvents.front().transportGeneration == 7);

		// ...while sendMidi sees only real MIDI. A marker leaking in here would be
		// pushed to the firmware as a 0x00 byte.
		expect(device.m_midiSent.size() == 1);
		expect(device.m_midiSent.front().type == MidiEventType::Midi);
		expect(device.m_midiSent.front().a == 0x90);
	}

	// The stamp is the offset in the block plus the samples of all blocks before it plus the extra latency
	void testStampOffsetCountsProcessedSamples()
	{
		StampingDevice device;
		device.setExtraLatencySamples(10);

		std::vector<SMidiEvent> midiOut;
		float buffer[8] = {};
		float* const ptr = buffer;
		const TAudioInputs in = {ptr, ptr, nullptr, nullptr};
		const TAudioOutputs out = {ptr, ptr};

		for (uint32_t block = 0; block < 3; ++block)
			device.process(in, out, 8, {SMidiEvent(MidiEventSource::Host, 0x90, 0x40, 0x7f, 3)}, midiOut);

		expect(device.m_midiSent.size() == 3);
		expect(device.m_midiSent[0].offset == 3 + 0 + 10);
		expect(device.m_midiSent[1].offset == 3 + 8 + 10);
		expect(device.m_midiSent[2].offset == 3 + 16 + 10);
	}

	// A console's runner hands the device its MIDI with the next block and decodes what the device sends back
	void testDeviceRunner()
	{
		PanelDevice device;
		DeviceRunner runner(device, 16);

		std::vector<uint8_t> lcd;
		runner.getFrontPanel().evLcdText.addListener([&](uint32_t, uint32_t, const std::vector<uint8_t>& _cells) { lcd = _cells; });

		runner.addMidi(SMidiEvent(MidiEventSource::Host, 0x90, 0x40, 0x7f));
		runner.process();
		runner.process();

		expect(device.m_midiSent.size() == 1);
		expect(runner.getFramesProcessed() == 32);
		expect(lcd == std::vector<uint8_t>{'H', 'i'});
	}

	// Hardware can deliver one dump in several chunks. Plugin reassembles them, and the device has to
	// receive the whole message exactly once - not the reassembled message plus the raw pieces it was
	// built from. A continuation with no start before it is garbage and must not reach the device.
	void testChunkedSysexReachesDeviceOnce()
	{
		TestDevice device;
		Plugin plugin(&device, [](auto* _d) { return _d; });
		plugin.setMidiClockEnabled(false);
		plugin.setHostSamplerate(44100.0f, 0.0f);
		plugin.setBlockSize(8);

		auto chunk = [](const std::initializer_list<uint8_t> _bytes)
		{
			SMidiEvent ev(MidiEventSource::Physical);
			ev.sysex.assign(_bytes.begin(), _bytes.end());
			return ev;
		};

		plugin.addMidiEvent(chunk({0xf0, 0x00, 0x20}));	// start
		plugin.addMidiEvent(chunk({0x33, 0x01}));			// middle
		plugin.addMidiEvent(chunk({0x10, 0xf7}));			// end
		plugin.addMidiEvent(chunk({0x44, 0xf7}));			// a fragment with no start before it

		float left[8] = {};
		float right[8] = {};
		TAudioInputs in{};
		TAudioOutputs out{};
		out[0] = left;
		out[1] = right;
		for (int block = 0; block < 4; ++block)
			plugin.process(in, out, 8, 120.0f, 0.0f, false, false);

		expect(device.m_midiSent.size() == 1);
		const std::vector<uint8_t> expected{0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, 0xf7};
		const auto& received = device.m_midiSent.front().sysex;
		expect(std::vector<uint8_t>(received.begin(), received.end()) == expected);
	}

	// A host that leaves the input bus and an output bus inactive passes null for both. Plugin stands
	// in a buffer for each, and the input's has to stay silent: one shared buffer fed whatever the
	// device wrote to the missing output straight back into its input.
	void testMissingInputDoesNotHearMissingOutput()
	{
		FeedbackProbe device;
		Plugin plugin(&device, [](auto* _d) { return _d; });
		plugin.setMidiClockEnabled(false);
		plugin.setHostSamplerate(44100.0f, 0.0f);
		plugin.setBlockSize(8);

		float left[8] = {};
		float right[8] = {};
		TAudioInputs in{};
		TAudioOutputs out{};
		out[0] = left;
		out[1] = right;
		for (int block = 0; block < 4; ++block)
			plugin.process(in, out, 8, 120.0f, 0.0f, false, false);

		expect(!device.m_heardOutput);
	}
} // namespace

int main()
{
	baseLib::disableErrorDialogs();

	testTransportMarkerBypassesSendMidi();
	testStampOffsetCountsProcessedSamples();
	testDeviceRunner();
	testChunkedSysexReachesDeviceOnce();
	testMissingInputDoesNotHearMissingOutput();
	return 0;
}
