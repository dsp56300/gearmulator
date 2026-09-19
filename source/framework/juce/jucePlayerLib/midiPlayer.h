#pragma once

#include "synthLib/midi/songReset.h"

#include "synthLib/midiTypes.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace jucePlayer
{
    class MidiPlayer final
    {
    public:
        static constexpr double kEndTailSeconds = 4.0;

        // Maximum number of logical MIDI ports a song can address.
        static constexpr uint8_t kMaximumPortCount = 16;

        using ResetMode = synthLib::midi::ResetMode;
        // Let cleanup and reset traffic drain before submitting the song's setup.
        // Cleanup still runs with ResetMode::Off, so it needs the same head start.
        static constexpr uint32_t kResetSettleMs = 200;
        static constexpr uint32_t kMaximumSongGapMs = 60000;

        enum class State : uint8_t
        {
            Stopped,
            Playing,
            Paused
        };

        struct Entry
        {
            std::string path;
            std::string name;
            double durationSeconds = 0.0;
        };

        struct Status
        {
            State state = State::Stopped;
            int currentIndex = -1;
            double positionSeconds = 0.0;
            uint64_t revision = 0;
        };

        struct AddResult
        {
            size_t added = 0;
            std::vector<std::string> errors;
        };

        explicit MidiPlayer(uint8_t portCount = 1, ResetMode resetMode = ResetMode::Off);

        AddResult addFiles(const std::vector<std::string>& _paths);
        // Replaces the playlist only when every file can be read. This lets UI operations
        // such as loading a saved playlist fail without discarding the current one.
        AddResult replaceFiles(const std::vector<std::string>& _paths);
        bool move(size_t _from, size_t _to);
        bool remove(size_t _index);
        bool clear();

        void play(size_t _index);
        void togglePlayPause();
        void stop();

        void setResetMode(ResetMode _mode);
        ResetMode resetMode() const;
        void setSongGapMs(uint32_t _milliseconds);
        uint32_t songGapMs() const;
        void setPortCount(uint8_t _count);
        void setEndTailMs(uint32_t _milliseconds) { m_endTailMs.store(std::min(_milliseconds, 600000u)); }

        std::vector<Entry> entries() const;
        // Startup silence for a song, including cleanup, reset and opening setup.
        double preparationSeconds(size_t _index) const;
        uint64_t playlistRevision() const;
        Status status() const;

        // Called only by the audio thread. Appends sample-offset MIDI events and
        // never reads the filesystem or takes a mutex. Once a song's last event
        // has elapsed, rendering continues for the configured end tail before advancing.
        // Disabled playback consumes commands without starting songs, retaining the playlist.
        void processBlock(std::vector<synthLib::SMidiEvent>& _events, uint32_t _sampleCount, double _sampleRate,
                          bool _playbackEnabled = true);

    private:
        struct Song;
        struct Playlist;
        enum class Command : uint8_t
        {
            None,
            Play,
            Toggle,
            Stop
        };

        void publish(std::shared_ptr<Playlist> _playlist);
        void post(Command _command, size_t _index = 0);
        void applyCommand(uint64_t _command, std::vector<synthLib::SMidiEvent>& _events);
        void selectSong(size_t _index, bool _play, uint32_t _offset, std::vector<synthLib::SMidiEvent>& _events,
                        bool _automaticAdvance = false);
        void beginReset(uint32_t _offset, std::vector<synthLib::SMidiEvent>& _events);
        void beginOpening(uint32_t _offset, std::vector<synthLib::SMidiEvent>& _events);
        void discontinuity(uint32_t _offset, std::vector<synthLib::SMidiEvent>& _events);
        void silence(uint32_t _offset, std::vector<synthLib::SMidiEvent>& _events) const;
        void updatePublishedStatus();

        std::shared_ptr<const Playlist> m_playlist;
        std::atomic<uint64_t> m_command{0};
        std::atomic<ResetMode> m_resetMode{ResetMode::Off};
        std::atomic<uint32_t> m_songGapMs{0};
        std::atomic<uint32_t> m_endTailMs{4000};
        std::atomic<uint8_t> m_portCount{1};
        std::atomic<uint32_t> m_nextCommandGeneration{0};
        std::atomic<uint8_t> m_state{static_cast<uint8_t>(State::Stopped)};
        std::atomic<int> m_currentIndex{-1};
        std::atomic<uint64_t> m_positionMicros{0};
        std::atomic<uint64_t> m_statusRevision{0};

        // Audio-thread-owned transport state.
        enum class StartPhase : uint8_t
        {
            Ready,
            Gap,
            Reset,
            Arrangement,
            Opening
        };
        StartPhase m_startPhase = StartPhase::Ready;
        ResetMode m_startResetMode = ResetMode::Off;
        uint64_t m_waitSamples = 0;
        std::shared_ptr<const Playlist> m_audioPlaylist;
        std::shared_ptr<const Song> m_audioSong;
        uint64_t m_lastCommand = 0;
        uint64_t m_cursorSamples = 0;
        size_t m_eventIndex = 0;
        State m_audioState = State::Stopped;
        int m_audioIndex = -1;
        double m_lastSampleRate = 0.0;
    };
} // namespace jucePlayer
