// The host's transport start/stop/seek marker is not MIDI - it carries no status
// or data bytes. Device::process must hand it to onTransportDiscontinuity, never
// to sendMidi: devices push an event's bytes straight into their emulated UART,
// and a lone 0x00 there completes the firmware's running-status message, which
// on at least one device turned every "play" into a Program Change.
#include "device.h"

#include <cstdlib>
#include <vector>

namespace
{
    using namespace synthLib;

    class TestDevice final : public Device
    {
    public:
        TestDevice() : Device(DeviceCreateParams()) {}

        std::vector<SMidiEvent> midiSent;
        std::vector<SMidiEvent> transportEvents;

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
            midiSent.push_back(_ev);
            return true;
        }
        void onTransportDiscontinuity(const SMidiEvent& _ev) override
        {
            transportEvents.push_back(_ev);
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
        expect(device.transportEvents.size() == 1);
        expect(device.transportEvents.front().transportGeneration == 7);

        // ...while sendMidi sees only real MIDI. A marker leaking in here would be
        // pushed to the firmware as a 0x00 byte.
        expect(device.midiSent.size() == 1);
        expect(device.midiSent.front().type == MidiEventType::Midi);
        expect(device.midiSent.front().a == 0x90);
    }
} // namespace

int main()
{
    testTransportMarkerBypassesSendMidi();
    return 0;
}
