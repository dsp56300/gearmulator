// The host's transport start/stop/seek marker is not MIDI - it carries no status
// or data bytes. Device::process must hand it to onTransportDiscontinuity, never
// to sendMidi: devices push an event's bytes straight into their emulated UART,
// and a lone 0x00 there completes the firmware's running-status message, which
// on at least one device turned every "play" into a Program Change.
#include "consoleTools.h"
#include "device.h"
#include "midiRoutingMatrix.h"
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

	// Answers every SysEx right away, as a device does that keeps what is asked for in C++, and plays a note of its own
	class ReplyingDevice final : public TestDevice
	{
	protected:
		bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>& _response) override
		{
			if (!_ev.sysex.empty())
				_response.emplace_back(MidiEventSource::Device).sysex = {0xf0, 0x01, 0xf7};
			return true;
		}
		void readMidiOut(std::vector<SMidiEvent>& _midiOut) override
		{
			_midiOut.emplace_back(MidiEventSource::Device, 0x90, 0x40, 0x7f);
		}
	};

	// Its firmware answers F0 11 <n> F7 a block later with F0 12 <n> F7, and F0 13 <n> F7 with F0 14 <n> 00 F7 and
	// F0 14 <n> 01 F7 one block apart. F0 15 <n> F7 is answered in C++ right away, with what the firmware sends for
	// F0 11 <n> F7
	class FirmwareDevice final : public TestDevice
	{
	public:
		// sent with the next block, asked for or not
		std::vector<SysexBuffer> m_firmwareOut;

	protected:
		bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>& _response) override
		{
			const auto& s = _ev.sysex;
			if (s.size() != 4)
				return true;

			const auto n = s[2];

			switch (s[1])
			{
			case 0x11:
				expectReply(_ev.source, expectBytes({{1, 0x12}, {2, n}}));
				m_next.push_back({0xf0, 0x12, n, 0xf7});
				break;
			case 0x13:
				expectReply(_ev.source, [n](const SMidiEvent& _e)
				{
					const auto& r = _e.sysex;
					if (r.size() != 5 || r[1] != 0x14 || r[2] != n)
						return ReplyMatch::None;
					return r[3] ? ReplyMatch::Complete : ReplyMatch::Partial;
				});
				m_next.push_back({0xf0, 0x14, n, 0x00, 0xf7});
				m_later.push_back({0xf0, 0x14, n, 0x01, 0xf7});
				break;
			case 0x15:
				_response.emplace_back(MidiEventSource::Device).sysex = {0xf0, 0x12, n, 0xf7};
				break;
			default:;
			}
			return true;
		}

		void readMidiOut(std::vector<SMidiEvent>& _midiOut) override
		{
			for (auto& s : m_firmwareOut)
				_midiOut.emplace_back(MidiEventSource::Device).sysex = std::move(s);

			m_firmwareOut = std::move(m_next);
			m_next = std::move(m_later);
			m_later.clear();
		}

	private:
		std::vector<SysexBuffer> m_next;
		std::vector<SysexBuffer> m_later;
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

	// A device's reply goes back to whoever asked and to the editor. A Virus on the physical port stored both RAM banks
	// that the editor had asked the emulator for. What the device plays goes wherever the routing sends it
	void testRepliesGoBackToWhoAsked()
	{
		ReplyingDevice device;

		auto request = [](const MidiEventSource _source)
		{
			SMidiEvent ev(_source);
			ev.sysex = {0xf0, 0x02, 0xf7};
			return ev;
		};

		std::vector<SMidiEvent> midiOut;
		float buffer[8] = {};
		float* const ptr = buffer;
		const TAudioInputs in = {ptr, ptr, nullptr, nullptr};
		const TAudioOutputs out = {ptr, ptr};

		device.process(in, out, 8, {request(MidiEventSource::Editor), request(MidiEventSource::Physical)}, midiOut);

		expect(midiOut.size() == 3);
		const auto& editorReply = midiOut[0];
		const auto& physicalReply = midiOut[1];
		const auto& note = midiOut[2];

		expect(editorReply.replyTo == MidiEventSource::Editor);
		expect(physicalReply.replyTo == MidiEventSource::Physical);
		expect(note.replyTo == MidiEventSource::Unknown);

		MidiRoutingMatrix matrix;
		matrix.setEnabled(MidiEventSource::Device, MidiEventSource::Host, MidiRoutingMatrix::EventType::All, true);

		expect(matrix.enabled(editorReply, MidiEventSource::Editor));
		expect(!matrix.enabled(editorReply, MidiEventSource::Physical));
		expect(!matrix.enabled(editorReply, MidiEventSource::Host));

		// a librarian on the physical port gets its answer, the editor sees it too
		expect(matrix.enabled(physicalReply, MidiEventSource::Physical));
		expect(matrix.enabled(physicalReply, MidiEventSource::Editor));
		expect(!matrix.enabled(physicalReply, MidiEventSource::Host));

		expect(matrix.enabled(note, MidiEventSource::Physical));
		expect(matrix.enabled(note, MidiEventSource::Host));

		// the routing still decides whether a reply may go there at all
		matrix.setEnabled(MidiEventSource::Device, MidiEventSource::Physical, MidiRoutingMatrix::EventType::SysEx, false);
		expect(!matrix.enabled(physicalReply, MidiEventSource::Physical));
	}

	// What a firmware answers later is a reply to whoever asked, like what a device answers in C++. A JP-8000 on the
	// physical port got its system area written with the firmware's answer to the editor's RQ1
	void testFirmwareRepliesGoBackToWhoAsked()
	{
		FirmwareDevice device;

		std::vector<SMidiEvent> midiOut;
		float buffer[4096] = {};
		float* const ptr = buffer;
		const TAudioInputs in = {ptr, ptr, nullptr, nullptr};
		const TAudioOutputs out = {ptr, ptr};

		auto process = [&](const std::vector<SMidiEvent>& _midiIn = {})
		{
			device.process(in, out, std::size(buffer), _midiIn, midiOut);
		};

		auto request = [](const MidiEventSource _source, const uint8_t _command, const uint8_t _n)
		{
			SMidiEvent ev(_source);
			ev.sysex = {0xf0, _command, _n, 0xf7};
			return ev;
		};

		auto find = [&](const std::initializer_list<uint8_t> _sysex) -> const SMidiEvent&
		{
			const auto it = std::find_if(midiOut.begin(), midiOut.end(), [&](const SMidiEvent& _e)
			{
				return std::equal(_e.sysex.begin(), _e.sysex.end(), _sysex.begin(), _sysex.end());
			});
			expect(it != midiOut.end());
			return *it;
		};

		// the editor's request answered in C++ looks like the firmware's answer to the physical port's: it stays the
		// editor's, and the firmware's answer is the physical port's
		process({request(MidiEventSource::Editor, 0x11, 1), request(MidiEventSource::Physical, 0x11, 2),
			request(MidiEventSource::Physical, 0x11, 4), request(MidiEventSource::Editor, 0x15, 4),
			request(MidiEventSource::Editor, 0x13, 5)});

		expect(midiOut.size() == 1);
		expect(find({0xf0, 0x12, 4, 0xf7}).replyTo == MidiEventSource::Editor);

		// asked for by nobody
		device.m_firmwareOut.push_back({0xf0, 0x12, 3, 0xf7});

		process();

		expect(midiOut.size() == 5);
		expect(find({0xf0, 0x12, 1, 0xf7}).replyTo == MidiEventSource::Editor);
		expect(find({0xf0, 0x12, 2, 0xf7}).replyTo == MidiEventSource::Physical);
		expect(find({0xf0, 0x12, 4, 0xf7}).replyTo == MidiEventSource::Physical);
		expect(find({0xf0, 0x14, 5, 0x00, 0xf7}).replyTo == MidiEventSource::Editor);
		expect(find({0xf0, 0x12, 3, 0xf7}).replyTo == MidiEventSource::Unknown);

		MidiRoutingMatrix matrix;
		expect(!matrix.enabled(find({0xf0, 0x12, 1, 0xf7}), MidiEventSource::Physical));
		expect(matrix.enabled(find({0xf0, 0x12, 2, 0xf7}), MidiEventSource::Physical));
		expect(matrix.enabled(find({0xf0, 0x12, 3, 0xf7}), MidiEventSource::Physical));

		// a complete answer ends its request: the same dump again is nobody's
		device.m_firmwareOut.push_back({0xf0, 0x12, 1, 0xf7});

		process();

		expect(midiOut.size() == 2);
		expect(find({0xf0, 0x14, 5, 0x01, 0xf7}).replyTo == MidiEventSource::Editor);
		expect(find({0xf0, 0x12, 1, 0xf7}).replyTo == MidiEventSource::Unknown);

		// an answer that takes a while is still one, a request that nothing answers for a few seconds is forgotten
		auto answerAfter = [&](const uint8_t _n, const float _seconds)
		{
			process({request(MidiEventSource::Editor, 0x11, _n)});
			device.m_firmwareOut.clear();

			for (size_t samples = 0; samples < static_cast<size_t>(_seconds * device.getSamplerate()); samples += std::size(buffer))
				process();

			device.m_firmwareOut.push_back({0xf0, 0x12, _n, 0xf7});
			process();
			return find({0xf0, 0x12, _n, 0xf7}).replyTo;
		};

		expect(answerAfter(6, 2.0f) == MidiEventSource::Editor);
		expect(answerAfter(7, 4.0f) == MidiEventSource::Unknown);
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
	testRepliesGoBackToWhoAsked();
	testFirmwareRepliesGoBackToWhoAsked();
	return 0;
}
