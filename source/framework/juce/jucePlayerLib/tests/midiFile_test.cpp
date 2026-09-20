#include "jucePlayerLib/midiFile.h"
#include "cpu/common/test_util.hpp"
#include "jucePlayerLib/midiPlayer.h"
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

    void rcpPlaylist(const Fixtures& files)
    {
        std::vector<uint8_t> data(0x586 + 0x2c);
        const std::string signature = "RCM-PC98V2.0(C)COME ON MUSIC";
        std::copy(signature.begin(), signature.end(), data.begin());
        data[0x1c0] = 48;
        data[0x1c1] = 120;
        data[0x1c2] = data[0x1c3] = 4;
        data[0x1e6] = 1;
        // The same pitch overlaps on A1 and B1 after an in-track port change.
        const uint8_t commands[] = {60, 12, 48, 100, 0xe6, 0, 17, 0, 60, 12, 48, 100, 0xfe, 0, 0, 0};
        data.insert(data.end(), std::begin(commands), std::end(commands));
        data[0x586] = 0x2c + sizeof(commands);
        const auto path = files.directory.getChildFile(juce::String::fromUTF8("音楽-é.R36"));
        CHECK(path.replaceWithData(data.data(), data.size()));
        const auto name = path.getFullPathName().toStdString();
        CHECK(midiFile::isSupported(name));
        std::vector<synthLib::midi::Event> decoded;
        std::string error;
        CHECK(midiFile::read(name, decoded, error));
        CHECK(midiFile::read(path.getRelativePathFrom(juce::File::getCurrentWorkingDirectory()).toStdString(), decoded,
                             error));
        CHECK_EQ(decoded.size(), 4u);
        if (decoded.size() == 4)
        {
            CHECK_EQ(decoded[0].port, 0);
            CHECK_EQ(decoded[1].port, 1);
            CHECK_EQ(decoded[2].port, 0);
            CHECK_EQ(decoded[3].port, 1);
            CHECK(std::abs(decoded[2].seconds - 0.5) < 1e-9);
            CHECK(std::abs(decoded[3].seconds - 0.625) < 1e-9);
        }

        // RCP v2 Tr.Excl puts its payload in F7 continuation records.  The
        // first record's two operands are not part of the generated SysEx.
        data.assign(0x586 + 0x2c, 0);
        std::copy(signature.begin(), signature.end(), data.begin());
        data[0x1c0] = 48;
        data[0x1c1] = 120;
        data[0x1c2] = data[0x1c3] = 4;
        data[0x1e6] = 1;
        const uint8_t trackExclusive[] = {0x98, 1, 2, 0, 0xf7, 0, 0x41, 0x10, 0xf7, 0, 0x42, 0x12,
                                           0xf7, 0, 0x40, 0, 0xfe, 0, 0, 0};
        data.insert(data.end(), std::begin(trackExclusive), std::end(trackExclusive));
        data[0x586] = 0x2c + sizeof(trackExclusive);
        CHECK(synthLib::midi::readRcp(data, decoded, error));
        CHECK_EQ(decoded.size(), 1u);
        if (decoded.size() == 1)
        {
            const std::vector<uint8_t> expected{0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0, 0xf7};
            CHECK(decoded[0].bytes == expected);
            CHECK(std::abs(decoded[0].seconds) < 1e-9);
        }
        MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
        CHECK_EQ(player.addFiles({files.paths[0], name}).added, 2u);
        player.setPortCount(2);
        player.play(1);
        // Song time is offset by the settle the Gs reset holds before the song starts.
        constexpr auto settle = MidiPlayer::kResetSettleMs;
        const auto played = block(player, settle + 700);
        CHECK_EQ(noteCount(played), 2);
        CHECK(std::any_of(played.begin(), played.end(), [](const auto& e)
                          { return e.a == 0x80 && e.b == 60 && e.port == 1 && e.offset == settle + 625; }));
        data.resize(20);
        CHECK(path.replaceWithData(data.data(), data.size()));
        const auto failed = player.addFiles({name});
        CHECK_EQ(failed.added, 0u);
        CHECK_EQ(failed.errors.size(), 1u);
        CHECK_EQ(player.entries().size(), 2u);
    }

    struct SmfTrack
    {
        std::vector<uint8_t> body;
        int declaredLength = -1; // -1 states the body's own size
    };

    // 96 ticks per quarter note, one chunk per track. A chunk length that does not match its body
    // is how a real file ends up describing a track that is not there.
    std::vector<uint8_t> smfFixture(const std::vector<SmfTrack>& tracks)
    {
        const auto count = static_cast<uint8_t>(tracks.size());
        std::vector<uint8_t> data = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, count, 0, 0x60};
        for (const auto& track : tracks)
        {
            const auto length = track.declaredLength < 0 ? static_cast<uint32_t>(track.body.size())
                                                         : static_cast<uint32_t>(track.declaredLength);
            data.insert(data.end(), {'M', 'T', 'r', 'k', static_cast<uint8_t>(length >> 24),
                                     static_cast<uint8_t>(length >> 16), static_cast<uint8_t>(length >> 8),
                                     static_cast<uint8_t>(length)});
            data.insert(data.end(), track.body.begin(), track.body.end());
        }
        return data;
    }

    // A meta or SysEx header that the file, or its track chunk, ends in the middle of. The reader
    // used to index the body regardless of how much of it was there: the truncated ones read past
    // the buffer, and a body starting past the chunk end made [body, end) run backwards, which
    // threw std::length_error out of std::string::assign and terminated the process. Every shape
    // below has to come back as an answer rather than a crash.
    void smfTruncatedHeaders()
    {
        std::vector<synthLib::midi::Event> decoded;
        std::string error;

        const auto answered = [&decoded, &error](const std::vector<uint8_t>& _data)
        {
            const bool ok = synthLib::midi::readSmf(_data, decoded, error);
            CHECK(ok == !decoded.empty());
            CHECK(ok || !error.empty());
        };

        for (const std::vector<uint8_t>& body : {std::vector<uint8_t>{0x00, 0xff, 0x51, 0x03},        // set tempo
                                                 std::vector<uint8_t>{0x00, 0xff, 0x21, 0x01},        // midi port
                                                 std::vector<uint8_t>{0x00, 0xff, 0x03, 0x05},        // track name
                                                 std::vector<uint8_t>{0x00, 0xf0, 0x05},              // sysex
                                                 std::vector<uint8_t>{0x00, 0xf7, 0x05}})             // sysex escape
        {
            // The chunk claims more than the file holds, so nothing follows the header at all.
            answered(smfFixture({{body, static_cast<int>(body.size()) + 3}}));
            // The chunk ends inside the header, so the body would start past the end of the track.
            answered(smfFixture({{body, 2}}));
        }

        // A meta whose own length reaches past the end of its chunk keeps the default tempo rather
        // than reading the bytes of whatever follows the track.
        const std::vector<uint8_t> notes = {0x00, 0x90, 60, 100, 0x60, 0x80, 60, 0x00, 0x00, 0xff, 0x2f, 0x00};
        const auto data = smfFixture({{{0x00, 0xff, 0x51, 0x03}}, {notes}});
        CHECK(synthLib::midi::readSmf(data, decoded, error));
        CHECK_EQ(decoded.size(), 2u);
        if (decoded.size() == 2)
            CHECK(std::abs(decoded.back().seconds - 0.5) < 1e-9); // 120 bpm, not the following chunk's header
    }

    void put16(std::vector<uint8_t>& data, size_t offset, uint16_t value)
    {
        data[offset] = static_cast<uint8_t>(value);
        data[offset + 1] = static_cast<uint8_t>(value >> 8);
    }

    std::vector<uint8_t> g36Fixture(const std::vector<std::array<uint8_t, 6>>& events)
    {
        std::vector<uint8_t> data(0xc98 + 46);
        const std::string signature = "COME ON MUSIC RECOMPOSER RCP3.0";
        std::copy(signature.begin(), signature.end(), data.begin());
        put16(data, 0x208, 1);
        put16(data, 0x20a, 480);
        put16(data, 0x20c, 120);
        data[0x20e] = data[0x20f] = 4;
        for (const auto& event : events)
            data.insert(data.end(), event.begin(), event.end());
        const auto length = data.size() - 0xc98;
        put16(data, 0xc98, static_cast<uint16_t>(length));
        put16(data, 0xc9a, static_cast<uint16_t>(length >> 16));
        return data;
    }

    void g36Playback(const Fixtures& files)
    {
        // Six-byte events store velocity before 16-bit step and gate times.
        auto data = g36Fixture(
            {{60, 100, 0xe0, 1, 0xc0, 3}, {0xe6, 0, 0, 0, 17, 0}, {60, 100, 0xe0, 1, 0xc0, 3}, {0xfe, 0, 0, 0, 0, 0}});
        data[0x211] = 2;
        data[0xc98 + 7] = 1;
        data[0xc98 + 8] = 0xd0; // ST+ = -48 ticks
        const auto path = files.directory.getChildFile(juce::String::fromUTF8("音楽-é.G36"));
        CHECK(path.replaceWithData(data.data(), data.size()));
        CHECK(midiFile::isSupported(path.getFullPathName().toStdString()));
        CHECK(juce::String(midiFile::fileFilter).contains("*.g36"));
        std::vector<synthLib::midi::Event> decoded;
        std::string error;
        CHECK(midiFile::read(path.getFullPathName().toStdString(), decoded, error));
        CHECK_EQ(decoded.size(), 4u);
        if (decoded.size() == 4)
        {
            const double times[] = {0.0, 0.45, 0.95, 1.45};
            for (size_t i = 0; i < 4; ++i)
            {
                CHECK(std::abs(decoded[i].seconds - times[i]) < 1e-9);
                CHECK_EQ(decoded[i].port, i % 2);
                CHECK_EQ(decoded[i].bytes[1], 63);
            }
        }
        MidiPlayer player(4, MidiPlayer::ResetMode::Gs);
        CHECK_EQ(player.addFiles({path.getFullPathName().toStdString()}).added, 1u);
        player.setPortCount(2);
        player.setResetMode(MidiPlayer::ResetMode::Off);
        player.play(0);
        CHECK_EQ(noteCount(block(player, 1600)), 2);

        data = g36Fixture({{0x98, 6, 0, 0, 5, 0},
                           {0xf7, 0x41, 0x10, 0x42, 0x12, 0x83},
                           {0xf7, 0x40, 0, 0x80, 0x81, 0x84},
                           {0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0xf7},
                           {0x90, 6, 0, 0, 5, 0},
                           {0xfe, 0, 0, 0, 0, 0}});
        // The G36 user template has 23 label bytes and 25 payload bytes.
        const uint8_t user[] = {0x41, 0x10, 0x42, 0x12, 0x83, 0x40, 0, 0x80, 0x81, 0x84, 0xf7};
        std::copy(std::begin(user), std::end(user), data.begin() + 0xb18 + 23);
        CHECK(synthLib::midi::readRcp(data, decoded, error));
        CHECK_EQ(decoded.size(), 2u);
        if (decoded.size() == 2)
        {
            const std::vector<uint8_t> expected{0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0, 5, 6, 0x35, 0xf7};
            CHECK(decoded[0].bytes == expected);
            CHECK(decoded[1].bytes == expected);
        }

        // Repeat the first measure, then repeat that reference, with a 16-bit tempo ratio.  FC holds the
        // measure number and the measure's byte offset from the track start: 46 + 6 * record.
        data = g36Fixture({{0xe7, 0, 0, 0, 0, 1},
                           {60, 100, 0xe0, 1, 0xe0, 1},
                           {0xfd, 0, 0, 0, 0, 0},
                           {0xfc, 0, 0, 0, 46, 0},
                           {0xfc, 0, 1, 0, 64, 0},
                           {0xfe, 0, 0, 0, 0, 0}});
        CHECK(synthLib::midi::readRcp(data, decoded, error));
        CHECK_EQ(decoded.size(), 6u);
        if (decoded.size() == 6)
            CHECK(std::abs(decoded.back().seconds - 0.375) < 1e-9);

        // A forward reference to the final measure returns after its pending note-off.
        data = g36Fixture({{0xfc, 0, 2, 0, 64, 0},
                           {62, 100, 0xe0, 1, 0xe0, 1},
                           {0xfd, 0, 0, 0, 0, 0},
                           {60, 100, 0xe0, 1, 0xc0, 3},
                           {0xfe, 0, 0, 0, 0, 0}});
        CHECK(synthLib::midi::readRcp(data, decoded, error));
        CHECK_EQ(decoded.size(), 6u);
        if (decoded.size() == 6)
        {
            CHECK_EQ(decoded[2].bytes[1], 62);
            CHECK(std::abs(decoded[2].seconds - 1.0) < 1e-9);
            CHECK(std::abs(decoded.back().seconds - 2.5) < 1e-9);
        }

        // An offset between records is not a reference.
        data = g36Fixture({{0xfc, 0, 0, 0, 49, 0}, {60, 100, 0xe0, 1, 0xe0, 1}, {0xfe, 0, 0, 0, 0, 0}});
        CHECK(!synthLib::midi::readRcp(data, decoded, error));

        // Graduation 255 spans 15 ticks at 48 PPQN, in integer-BPM steps.
        data = g36Fixture({{0xe7, 255, 160, 0, 32, 0}, {60, 100, 0xe0, 1, 0xe0, 1}, {0xfe, 0, 0, 0, 0, 0}});
        CHECK(synthLib::midi::readRcp(data, decoded, error));
        CHECK_EQ(decoded.size(), 2u);
        if (decoded.size() == 2)
        {
            double startSeconds = 0;
            for (int bpm : {120, 112, 104, 96, 88, 80, 72})
                startSeconds += 20.0 * 60.0 / (bpm * 480.0);
            startSeconds += 10.0 * 60.0 / (64.0 * 480.0) + 10.0 / 480.0;
            CHECK(std::abs(decoded[0].seconds - startSeconds) < 1e-9);
            CHECK(std::abs(decoded[1].seconds - (startSeconds + 1.0)) < 1e-9);
        }
        // An abrupt change cancels the remaining graduation.
        data = g36Fixture(
            {{0xe7, 255, 50, 0, 32, 0}, {0xe7, 0, 0, 0, 128, 0}, {60, 100, 0xe0, 1, 0xe0, 1}, {0xfe, 0, 0, 0, 0, 0}});
        CHECK(synthLib::midi::readRcp(data, decoded, error));
        CHECK_EQ(decoded.size(), 2u);
        if (decoded.size() == 2)
        {
            const auto startSeconds = 20.0 / 960.0 + 20.0 * 60.0 / (112.0 * 480.0) + 10.0 * 60.0 / (104.0 * 480.0);
            CHECK(std::abs(decoded[0].seconds - startSeconds) < 1e-9);
            CHECK(std::abs(decoded[1].seconds - (startSeconds + 0.25)) < 1e-9);
        }

        // Positive loop counts above 255 are finite in G36.
        data = g36Fixture({{0xf9, 0, 0, 0, 0, 0}, {60, 100, 1, 0, 1, 0}, {0xf8, 0, 0, 1, 0, 0}, {0xfe, 0, 0, 0, 0, 0}});
        CHECK(synthLib::midi::readRcp(data, decoded, error));
        CHECK_EQ(decoded.size(), 512u);

        // Exercise the upper half of the 32-bit track length and 16-bit header tempo.
        std::vector<std::array<uint8_t, 6>> longTrack(11000, {0xf6, 0, 0, 0, 0, 0});
        longTrack.push_back({60, 100, 0xe0, 1, 0xe0, 1});
        longTrack.push_back({0xfe, 0, 0, 0, 0, 0});
        data = g36Fixture(longTrack);
        put16(data, 0x20c, 300);
        CHECK(synthLib::midi::readRcp(data, decoded, error));
        CHECK_EQ(decoded.size(), 2u);
        if (decoded.size() == 2)
            CHECK(std::abs(decoded.back().seconds - 0.2) < 1e-9);

        const auto valid = data;
        for (const size_t length : {size_t(20), size_t(0xc98), size_t(0xc98 + 45), valid.size() - 1})
        {
            data.assign(valid.begin(), valid.begin() + length);
            CHECK(!synthLib::midi::readRcp(data, decoded, error));
            CHECK(decoded.empty());
            CHECK(!error.empty());
        }
        data = valid;
        put16(data, 0x208, 0x100); // Track count must not truncate to eight bits.
        CHECK(!synthLib::midi::readRcp(data, decoded, error));
        data = valid;
        put16(data, 0xc98, 1);
        put16(data, 0xc9a, 0);
        CHECK(!synthLib::midi::readRcp(data, decoded, error));
        data = g36Fixture({{60, 100, 1, 0, 1, 0}, {0xfc, 0, 0, 0, 0xff, 0xff}, {0xfe, 0, 0, 0, 0, 0}});
        CHECK(!synthLib::midi::readRcp(data, decoded, error));
        CHECK(decoded.empty());
        CHECK(error.find("measure") != std::string::npos);
    }

} // namespace

void checkMidiFiles()
{
    Fixtures files;
    rcpPlaylist(files);
    smfTruncatedHeaders();
    g36Playback(files);
}
