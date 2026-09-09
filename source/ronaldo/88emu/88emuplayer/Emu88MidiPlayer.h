#pragma once

#include "synthLib/midiTypes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emu88Player
{
	class MidiPlayer final
	{
	public:
		static constexpr double kEndTailSeconds = 4.0;

		// Logical MIDI ports a song can address. The SC-8850 exposes four USB
		// cables (part groups A-D); boards with fewer inputs fold the extra
		// ports onto the ones they have.
		static constexpr uint8_t kPortCount = 4;

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

		MidiPlayer();

		AddResult addFiles(const std::vector<std::string>& _paths);
		bool move(size_t _from, size_t _to);
		bool remove(size_t _index);
		bool clear();

		void play(size_t _index);
		void togglePlayPause();
		void stop();

		std::vector<Entry> entries() const;
		uint64_t playlistRevision() const;
		Status status() const;

		// Called only by the audio thread. Appends sample-offset MIDI events and
		// never reads the filesystem or takes a mutex. Once a song's last event
		// has elapsed, rendering continues for kEndTailSeconds before advancing.
		void processBlock(std::vector<synthLib::SMidiEvent>& _events,
		                  uint32_t _sampleCount, double _sampleRate);

	private:
		struct Song;
		struct Playlist;
		enum class Command : uint8_t { None, Play, Toggle, Stop };

		void publish(std::shared_ptr<Playlist> _playlist);
		void post(Command _command, size_t _index = 0);
		void applyCommand(uint64_t _command, std::vector<synthLib::SMidiEvent>& _events);
		void selectSong(size_t _index, bool _play, uint32_t _offset,
		                std::vector<synthLib::SMidiEvent>& _events);
		void silence(uint32_t _offset, std::vector<synthLib::SMidiEvent>& _events) const;
		void updatePublishedStatus();

		std::shared_ptr<const Playlist> m_playlist;
		std::atomic<uint64_t> m_command{0};
		std::atomic<uint32_t> m_nextCommandGeneration{0};
		std::atomic<uint8_t> m_state{static_cast<uint8_t>(State::Stopped)};
		std::atomic<int> m_currentIndex{-1};
		std::atomic<uint64_t> m_positionMicros{0};
		std::atomic<uint64_t> m_statusRevision{0};

		// Audio-thread-owned transport state.
		std::shared_ptr<const Playlist> m_audioPlaylist;
		std::shared_ptr<const Song> m_audioSong;
		uint64_t m_lastCommand = 0;
		uint64_t m_cursorSamples = 0;
		size_t m_eventIndex = 0;
		State m_audioState = State::Stopped;
		int m_audioIndex = -1;
		double m_lastSampleRate = 0.0;
	};
}
