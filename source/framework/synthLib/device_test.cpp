// The host's transport start/stop/seek marker is not MIDI - it carries no status
// or data bytes. Device::process must hand it to onTransportDiscontinuity, never
// to sendMidi: devices push an event's bytes straight into their emulated UART,
// and a lone 0x00 there completes the firmware's running-status message, which
// on at least one device turned every "play" into a Program Change.
#include "device.h"
#include "plugin.h"

#include <cstdlib>
#include <vector>

namespace
{
	using namespace synthLib;

	class TestDevice final : public Device
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
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 0; }

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
} // namespace

int main()
{
	testTransportMarkerBypassesSendMidi();
	testChunkedSysexReachesDeviceOnce();
	return 0;
}
