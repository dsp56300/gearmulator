#include "jucePlayerLib/midiPlayer.h"
#include "cpu/common/test_util.hpp"
#include "jucePlayerLib/midiFile.h"
#include "juce_core/juce_core.h"
#include "synthLib/device.h"
#include "synthLib/midi/midiFile.h"
#include "synthLib/plugin.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace jucePlayer;
using namespace synthLib;
using namespace test;

void checkMidiFiles();

namespace
{
    struct Fixtures
    {
        juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getNonexistentChildFile("midi-player-transitions", "", false);
        std::vector<std::string> paths;
        Fixtures()
        {
            CHECK(directory.createDirectory().wasOk());
            for (uint8_t note : {60, 61, 62})
            {
                // Note-on at time zero; note-off at 500 ms, then end of track.
                const uint8_t bytes[] = {'M',  'T', 'h', 'd',  0,    0,   0, 6,    0,    0,  0, 1,
                                         0,    96,  'M', 'T',  'r',  'k', 0, 0,    0,    12, 0, 0x90,
                                         note, 100, 96,  0x80, note, 0,   0, 0xff, 0x2f, 0};
                const auto file = directory.getChildFile(juce::String(note) + ".mid");
                CHECK(file.replaceWithData(bytes, sizeof(bytes)));
                paths.push_back(file.getFullPathName().toStdString());
            }
        }
        ~Fixtures() { (void)directory.deleteRecursively(); }
    };

    std::vector<SMidiEvent> block(MidiPlayer& player, uint32_t count, double rate = 1000)
    {
        std::vector<SMidiEvent> events;
        player.processBlock(events, count, rate);
        for (const auto& event : events)
            CHECK(event.offset < count);
        return events;
    }

    int noteCount(const std::vector<SMidiEvent>& events)
    {
        return static_cast<int>(
            std::count_if(events.begin(), events.end(), [](const auto& e) { return (e.a & 0xf0) == 0x90 && e.c; }));
    }

    void poweredOff(const Fixtures& files)
    {
        MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
        CHECK_EQ(player.addFiles(files.paths).added, 3u);
        player.setResetMode(MidiPlayer::ResetMode::Off);
        player.play(0);
        CHECK_EQ(noteCount(block(player, 100)), 1);
        std::vector<SMidiEvent> discarded;
        player.processBlock(discarded, 100, 1000, false);
        CHECK(player.status().state == MidiPlayer::State::Stopped);
        CHECK_EQ(player.status().positionSeconds, 0.0);
        CHECK_EQ(noteCount(discarded), 0);
        player.play(1);
        discarded.clear();
        player.processBlock(discarded, 100, 1000, false);
        CHECK_EQ(noteCount(discarded), 0);
        CHECK(player.status().state == MidiPlayer::State::Stopped);
        CHECK_EQ(noteCount(block(player, 100)), 0);
        CHECK_EQ(player.entries().size(), 3u);
        player.play(1);
        CHECK_EQ(noteCount(block(player, 100)), 1);
        CHECK_EQ(player.status().currentIndex, 1);
    }

    void resetTiming(const Fixtures& files)
    {
        for (const auto mode : {MidiPlayer::ResetMode::Off, MidiPlayer::ResetMode::Gm, MidiPlayer::ResetMode::Gs,
                                MidiPlayer::ResetMode::Mt32})
        {
            MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
            CHECK_EQ(player.addFiles(files.paths).added, 3u);
            player.setResetMode(mode);
            player.setPortCount(1);
            player.play(0);
            // One settle per block, so the song's first note falls at the head of the
            // block after the one that carries the reset. block() runs at 1 kHz.
            constexpr auto settle = MidiPlayer::kResetSettleMs;
            auto events = block(player, settle);
            CHECK(events.front().type == MidiEventType::TransportDiscontinuity);
            CHECK_EQ(
                std::count_if(events.begin(), events.end(), [](const auto& e) { return e.a == 0xb0 && e.b == 121; }),
                1);
            CHECK_EQ(noteCount(events), mode == MidiPlayer::ResetMode::Off ? 1 : 0);
            const auto reset =
                std::find_if(events.begin(), events.end(), [](const auto& e) { return !e.sysex.empty(); });
            CHECK((reset == events.end()) == (mode == MidiPlayer::ResetMode::Off));
            if (reset != events.end())
                CHECK_EQ(reset->sysex[1], mode == MidiPlayer::ResetMode::Gm ? 0x7e : 0x41);
            events = block(player, settle);
            CHECK_EQ(noteCount(events), mode == MidiPlayer::ResetMode::Gs || mode == MidiPlayer::ResetMode::Gm ? 1 : 0);
            if (mode == MidiPlayer::ResetMode::Mt32)
            {
                CHECK(
                    std::any_of(events.begin(), events.end(), [](const auto& e) { return e.a == 0xc9 && e.b == 127; }));
                CHECK(
                    std::any_of(events.begin(), events.end(), [](const auto& e) { return e.a == 0xc1 && e.b == 68; }));
                CHECK_EQ(noteCount(block(player, 1)), 1);
            }
            player.togglePlayPause();
            block(player, 100);
            player.togglePlayPause();
            events = block(player, 10);
            CHECK(events.empty()); // resume does not reset or restart the song
            player.stop();
            block(player, 10);
            player.togglePlayPause();
            events = block(player, 1);
            CHECK(events.front().type == MidiEventType::TransportDiscontinuity);
        }
    }

    void gapsAndCancellation(const Fixtures& files)
    {
        for (const double rate : {1000., 44100., 48000.})
            for (const uint32_t gap : {0u, 123u, 1001u})
            {
                MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
                player.addFiles(files.paths);
                player.setResetMode(MidiPlayer::ResetMode::Off);
                player.setSongGapMs(gap);
                player.play(0);
                const uint32_t end = static_cast<uint32_t>(std::ceil(4.5 * rate)) + 1;
                const uint32_t wait = static_cast<uint32_t>(std::ceil(gap * rate / 1000.));
                CHECK_EQ(noteCount(block(player, end, rate)), 1);
                if (wait)
                    CHECK_EQ(noteCount(block(player, wait, rate)), 0);
                const auto events = block(player, 1, rate);
                CHECK_EQ(noteCount(events), 1);
                CHECK_EQ(player.status().currentIndex, 1);
            }

        MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
        player.addFiles(files.paths);
        player.setResetMode(MidiPlayer::ResetMode::Off);
        player.setSongGapMs(1000);
        player.play(0);
        block(player, 4502);
        CHECK_EQ(player.status().currentIndex, 1);
        player.stop();
        CHECK_EQ(noteCount(block(player, 3000)), 0);
        player.play(2);
        auto events = block(player, 1);
        CHECK_EQ(noteCount(events), 1);
        CHECK_EQ(player.status().currentIndex, 2);
        player.play(0);
        block(player, 4502);
        player.play(2);
        CHECK_EQ(noteCount(block(player, 1)), 1); // explicit selection cancels the gap

        player.setResetMode(MidiPlayer::ResetMode::Gs);
        player.play(0);
        block(player, 50);
        player.stop();
        CHECK_EQ(noteCount(block(player, 1000)), 0);
        player.play(1);
        block(player, 50);
        // The rest of the settle, at twice the rate it was started on.
        CHECK_EQ(noteCount(block(player, (MidiPlayer::kResetSettleMs - 50) * 2, 2000)), 0);
        CHECK_EQ(noteCount(block(player, 1, 2000)), 1);
    }

    void reorderAndPorts(const Fixtures& files)
    {
        MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
        player.addFiles(files.paths);
        player.setResetMode(MidiPlayer::ResetMode::Off);
        player.play(0);
        block(player, 100);
        CHECK(player.move(0, 3));
        CHECK(block(player, 1).empty());
        CHECK_EQ(player.status().currentIndex, 2);
        CHECK(player.status().positionSeconds > 0.1);
        player.stop();
        const auto events = block(player, 1);
        for (int port = 0; port < 4; ++port)
            for (int channel = 0; channel < 16; ++channel)
                CHECK(std::any_of(events.begin(), events.end(), [=](const auto& e)
                                  { return e.port == port && e.a == (0xb0 | channel) && e.b == 120; }));
        for (int i = 0; i < 100; ++i)
        {
            player.play(i % 3);
            const auto switched = block(player, 1);
            CHECK_EQ(noteCount(switched), 1);
            const auto cleanup =
                std::find_if(switched.begin(), switched.end(), [](const auto& e) { return e.a == 0xb0 && e.b == 121; });
            const auto note = std::find_if(switched.begin(), switched.end(), [](const auto& e) { return e.a == 0x90; });
            CHECK(cleanup < note);
        }
    }

    class CaptureDevice final : public Device
    {
    public:
        CaptureDevice() : Device({}) {}
        std::vector<SMidiEvent> received;
        float getSamplerate() const override { return 32000; }
        bool isValid() const override { return true; }
        bool getState(std::vector<uint8_t>&, StateType) override { return false; }
        bool setState(const std::vector<uint8_t>&, StateType) override { return false; }
        uint32_t getChannelCountIn() override { return 0; }
        uint32_t getChannelCountOut() override { return 2; }
        bool setDspClockPercent(uint32_t) override { return false; }
        uint32_t getDspClockPercent() const override { return 100; }
        uint64_t getDspClockHz() const override { return 0; }

    protected:
        void onTransportDiscontinuity(const SMidiEvent& e) override { received.push_back(e); }
        bool sendMidi(const SMidiEvent& e, std::vector<SMidiEvent>&) override
        {
            received.push_back(e);
            return true;
        }
        void readMidiOut(std::vector<SMidiEvent>&) override {}
        void processAudio(const TAudioInputs&, const TAudioOutputs& out, size_t n) override
        {
            std::fill_n(out[0], n, 0.f);
            std::fill_n(out[1], n, 0.f);
        }
    };

    void engineGenerations()
    {
        CaptureDevice device;
        Plugin engine(&device, [](auto*) -> Device* { return nullptr; });
        engine.setHostSamplerate(32000, 0);
        engine.setBlockSize(256);
        engine.setMidiClockEnabled(false);
        SMidiEvent marker(MidiEventSource::Internal);
        marker.type = MidiEventType::TransportDiscontinuity;
        for (int i = 0; i < 3; ++i)
        {
            marker.offset = i * 64;
            engine.addMidiEvent(marker);
            SMidiEvent reset(MidiEventSource::Host);
            reset.sysex = {0xf0, 0x7e, 0x7f, 9, 1, 0xf7};
            reset.cancelOnTransportChange = true;
            reset.offset = marker.offset;
            engine.addMidiEvent(reset);
            engine.addMidiEvent(SMidiEvent(MidiEventSource::Host, 0x90, 60, 100, marker.offset));
        }
        float left[256]{}, right[256]{};
        TAudioOutputs outputs{};
        outputs[0] = left;
        outputs[1] = right;
        for (int i = 0; i < 8; ++i)
            engine.process({}, outputs, 256, 120, 0, false, false);
        CHECK_EQ(device.received.size(), 9u);
        for (size_t i = 0; i + 2 < device.received.size(); i += 3)
        {
            CHECK(device.received[i].type == MidiEventType::TransportDiscontinuity);
            CHECK_EQ(device.received[i].transportGeneration, i / 3 + 1);
            CHECK_EQ(device.received[i + 1].transportGeneration, device.received[i].transportGeneration);
            CHECK(device.received[i + 1].cancelOnTransportChange);
            CHECK_EQ(device.received[i + 2].transportGeneration, device.received[i].transportGeneration);
        }
    }
} // namespace

int main()
{
    Fixtures files;
    checkMidiFiles();
    poweredOff(files);
    resetTiming(files);
    gapsAndCancellation(files);
    reorderAndPorts(files);
    engineGenerations();
    return finish("midiPlayer");
}
