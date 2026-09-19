#include "jucePlayerLib/midiPlayer.h"
#include "cpu/common/test_util.hpp"
#include "jucePlayerLib/midiFile.h"
#include "juce_core/juce_core.h"
#include "synthLib/device.h"
#include "synthLib/midi/midiFile.h"
#include "synthLib/plugin.h"
#include "baseLib/os.h"

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
        CHECK_EQ(noteCount(block(player, MidiPlayer::kResetSettleMs + 100)), 1);
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
        CHECK_EQ(noteCount(block(player, MidiPlayer::kResetSettleMs + 100)), 1);
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
            CHECK_EQ(noteCount(events), 0);
            const auto reset =
                std::find_if(events.begin(), events.end(), [](const auto& e) { return !e.sysex.empty(); });
            CHECK((reset == events.end()) == (mode == MidiPlayer::ResetMode::Off));
            if (reset != events.end())
                CHECK_EQ(reset->sysex[1], mode == MidiPlayer::ResetMode::Gm ? 0x7e : 0x41);
            events = block(player, settle);
            CHECK_EQ(noteCount(events), mode != MidiPlayer::ResetMode::Mt32 ? 1 : 0);
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
                const uint32_t end = static_cast<uint32_t>(std::ceil((4.5 + MidiPlayer::kResetSettleMs / 1000.) * rate)) + 1;
                const uint32_t wait = static_cast<uint32_t>(std::ceil(gap * rate / 1000.));
                CHECK_EQ(noteCount(block(player, end, rate)), 1);
                if (wait)
                    CHECK_EQ(noteCount(block(player, wait, rate)), 0);
                CHECK_EQ(noteCount(block(player, static_cast<uint32_t>(std::ceil(MidiPlayer::kResetSettleMs * rate / 1000.)), rate)), 0);
                const auto events = block(player, 1, rate);
                CHECK_EQ(noteCount(events), 1);
                CHECK_EQ(player.status().currentIndex, 1);
            }

        MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
        player.addFiles(files.paths);
        player.setResetMode(MidiPlayer::ResetMode::Off);
        player.setSongGapMs(1000);
        player.play(0);
        block(player, 4502 + MidiPlayer::kResetSettleMs);
        CHECK_EQ(player.status().currentIndex, 1);
        player.stop();
        CHECK_EQ(noteCount(block(player, 3000)), 0);
        player.play(2);
        auto events = block(player, MidiPlayer::kResetSettleMs + 1);
        CHECK_EQ(noteCount(events), 1);
        CHECK_EQ(player.status().currentIndex, 2);
        player.play(0);
        block(player, 4502 + MidiPlayer::kResetSettleMs);
        player.play(2);
        CHECK_EQ(noteCount(block(player, MidiPlayer::kResetSettleMs + 1)), 1); // selection cancels the gap, but retains cleanup

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
        block(player, MidiPlayer::kResetSettleMs + 100);
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
            const auto switched = block(player, MidiPlayer::kResetSettleMs + 1);
            CHECK_EQ(noteCount(switched), 1);
            const auto cleanup =
                std::find_if(switched.begin(), switched.end(), [](const auto& e) { return e.a == 0xb0 && e.b == 121; });
            const auto note = std::find_if(switched.begin(), switched.end(), [](const auto& e) { return e.a == 0x90; });
            CHECK(cleanup < note);
        }
    }

    void replacePlaylist(const Fixtures& files)
    {
        MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
        CHECK_EQ(player.addFiles({files.paths[0]}).added, 1u);
        const auto original = player.entries();

        const auto rejected = player.replaceFiles({files.paths[1], "not-a-midi-file.mid"});
        CHECK_EQ(rejected.added, 1u);
        CHECK_EQ(rejected.errors.size(), 1u);
        CHECK(player.entries().size() == original.size());
        if(player.entries().size() == original.size())
            CHECK(player.entries()[0].path == original[0].path);

        const auto replaced = player.replaceFiles({files.paths[1], files.paths[2]});
        CHECK_EQ(replaced.added, 2u);
        CHECK(replaced.errors.empty());
        CHECK_EQ(player.entries().size(), 2u);
        if(player.entries().size() == 2)
        {
            CHECK(player.entries()[0].path == files.paths[1]);
            CHECK(player.entries()[1].path == files.paths[2]);
        }
    }

    void openingSetup(const Fixtures& files)
    {
        // Interleaved channel setup, an intentional PC after a note, a second port,
        // then a SysEx barrier. Only setup before each channel's first note can move.
        const std::vector<uint8_t> track{
            0, 0xb0, 0, 0, 0, 0xc0, 10, 0, 0x90, 60, 100,
            0, 0xc0, 20, 0, 0x90, 64, 100,
            0, 0xb1, 0, 0, 0, 0xc1, 30, 0, 0x91, 61, 100, 0, 0xb1, 7, 20,
            0, 0xff, 0x21, 1, 1, 0, 0xc0, 40, 0, 0x90, 65, 100,
            0, 0xf0, 5, 0x7e, 0x7f, 9, 1, 0xf7, 0, 0xc2, 50,
            96, 0x80, 65, 0, 0, 0xff, 0x2f, 0};
        auto writeTrack = [&](const std::vector<uint8_t>& bytes)
        {
            std::vector<uint8_t> data{'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k'};
            for (int shift = 24; shift >= 0; shift -= 8)
                data.push_back(static_cast<uint8_t>(bytes.size() >> shift));
            data.insert(data.end(), bytes.begin(), bytes.end());
            const auto file = files.directory.getChildFile("opening.mid");
            CHECK(file.replaceWithData(data.data(), data.size()));
            return file.getFullPathName().toStdString();
        };
        const auto path = writeTrack(track);
        for (const auto mode : {MidiPlayer::ResetMode::Off, MidiPlayer::ResetMode::Gs, MidiPlayer::ResetMode::Mt32})
            for (const double rate : {1000., 44100., 48000.})
                for (const uint32_t count : {1u, 127u, 512u})
                {
                    MidiPlayer player(2, mode);
                    CHECK_EQ(player.addFiles({path}).added, 1u);
                    const auto resetSamples = static_cast<uint32_t>(std::ceil(MidiPlayer::kResetSettleMs * rate / 1000.));
                    const auto setupAt = resetSamples * (mode == MidiPlayer::ResetMode::Mt32 ? 2 : 1);
                    // 12 setup bytes at 3125 bytes/s, plus 50 ms for firmware processing.
                    const auto songAt = setupAt + static_cast<uint32_t>(std::ceil(54 * rate / 1000.));
                    CHECK(std::abs(player.preparationSeconds(0) - (setupAt / rate + 0.054)) < 1e-9);
                    player.play(0);
                    std::vector<SMidiEvent> all;
                    for (uint32_t offset = 0; offset < songAt + rate * .51; offset += count)
                    {
                        auto events = block(player, count, rate);
                        for (auto& e : events)
                        {
                            e.offset += offset;
                            all.push_back(std::move(e));
                        }
                        if (offset + count <= songAt)
                            CHECK_EQ(player.status().positionSeconds, 0.0);
                    }
                    const auto find = [&](uint8_t a, uint8_t b, uint8_t port = 0)
                    {
                        return std::find_if(all.begin(), all.end(), [&](const auto& e)
                            { return e.a == a && e.b == b && e.port == port &&
                                (a != 0xb1 || b != 7 || e.c == 20); });
                    };
                    const auto checkAt = [&](uint8_t a, uint8_t b, uint8_t port, uint32_t when)
                    {
                        const auto e = find(a, b, port);
                        CHECK(e != all.end());
                        if (e != all.end()) CHECK_EQ(e->offset, when);
                    };
                    checkAt(0xc0, 10, 0, setupAt);
                    checkAt(0xc1, 30, 0, setupAt);
                    checkAt(0xc0, 40, 1, setupAt); // Same channel number, independent port.
                    checkAt(0xc0, 20, 0, songAt);
                    checkAt(0xb1, 7, 0, songAt);
                    checkAt(0xc2, 50, 1, songAt); // Must stay after the file's SysEx.
                    checkAt(0x80, 65, 1, songAt + static_cast<uint32_t>(std::llround(.5 * rate)));
                    CHECK(find(0x90, 60) < find(0xc0, 20));
                    CHECK(find(0xc0, 20) < find(0x90, 64));
                    CHECK_EQ(noteCount(all), 4);
                    CHECK_EQ(std::count_if(all.begin(), all.end(), [](const auto& e)
                        { return e.a == 0xc0 && e.b == 10; }), 1);
                }

        MidiPlayer player(2, MidiPlayer::ResetMode::Gs);
        CHECK_EQ(player.replaceFiles({path}).added, 1u);
        player.play(0);
        CHECK_EQ(noteCount(block(player, MidiPlayer::kResetSettleMs + 20)), 0);
        player.togglePlayPause();
        block(player, 1);
        player.togglePlayPause();
        CHECK_EQ(noteCount(block(player, MidiPlayer::kResetSettleMs + 54)), 0);
        CHECK_EQ(noteCount(block(player, 1)), 4); // Resume restarts preparation after cleanup.
        player.play(0);
        block(player, MidiPlayer::kResetSettleMs + 20);
        player.stop();
        CHECK_EQ(noteCount(block(player, 1000)), 0);

        player.play(0);
        block(player, MidiPlayer::kResetSettleMs + 20);
        CHECK_EQ(noteCount(block(player, 68, 2000)), 0); // 34 ms of setup remain at the new rate.
        CHECK_EQ(noteCount(block(player, 1, 2000)), 4);

        MidiPlayer transitions(2, MidiPlayer::ResetMode::Off);
        transitions.addFiles({path, path});
        transitions.setEndTailMs(0);
        transitions.setSongGapMs(23);
        transitions.play(0);
        const auto both = block(transitions, 1100);
        CHECK_EQ(noteCount(both), 8);
        std::vector<uint32_t> starts;
        for (const auto& e : both)
            if (e.a == 0x90 && e.b == 60 && e.c) starts.push_back(e.offset);
        CHECK(starts == std::vector<uint32_t>({254, 1032}));

        // A large setup needs a wire-length-dependent wait, not another fixed delay.
        std::vector<uint8_t> dense;
        for (int i = 0; i < 300; ++i)
            dense.insert(dense.end(), {0, 0xb0, 7, 100});
        dense.insert(dense.end(), {0, 0x90, 60, 100, 96, 0x80, 60, 0, 0, 0xff, 0x2f, 0});
        MidiPlayer large(1, MidiPlayer::ResetMode::Off);
        large.addFiles({writeTrack(dense)});
        large.play(0);
        constexpr auto wait = MidiPlayer::kResetSettleMs + 288 + 50;
        CHECK_EQ(noteCount(block(large, wait)), 0);
        CHECK_EQ(noteCount(block(large, 1)), 1);
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
    baseLib::disableErrorDialogs();

    Fixtures files;
    checkMidiFiles();
    poweredOff(files);
    resetTiming(files);
    gapsAndCancellation(files);
    reorderAndPorts(files);
	    replacePlaylist(files);
    openingSetup(files);
    engineGenerations();
    return finish("midiPlayer");
}
