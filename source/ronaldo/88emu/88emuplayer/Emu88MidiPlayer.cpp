#include "Emu88MidiPlayer.h"

#include "midifile.hpp"

#include "juce_core/juce_core.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace emu88Player
{
	struct MidiPlayer::Song
	{
		Entry info;
		std::vector<sc88smf::Event> events;
	};

	struct MidiPlayer::Playlist
	{
		std::vector<std::shared_ptr<const Song>> songs;
		uint64_t revision = 0;
	};

	namespace
	{
		constexpr uint64_t commandIndexMask = 0x00ffffffu;

		bool isMidiFile(const std::string& _path)
		{
			const auto extension = juce::File(_path).getFileExtension().toLowerCase();
			return extension == ".mid" || extension == ".midi";
		}
	}

	MidiPlayer::MidiPlayer()
	{
		auto playlist = std::make_shared<Playlist>();
		std::atomic_store_explicit(&m_playlist, std::shared_ptr<const Playlist>(playlist),
		                           std::memory_order_release);
	}

	MidiPlayer::AddResult MidiPlayer::addFiles(const std::vector<std::string>& _paths)
	{
		AddResult result;
		auto current = std::atomic_load_explicit(&m_playlist, std::memory_order_acquire);
		auto next = std::make_shared<Playlist>();
		if(current)
			next->songs = current->songs;

		for(const auto& path : _paths)
		{
			if(!isMidiFile(path))
			{
				result.errors.push_back(juce::File(path).getFileName().toStdString() +
				                        ": expected a .mid or .midi file");
				continue;
			}

			auto song = std::make_shared<Song>();
			std::string error;
			if(!sc88smf::read(path, song->events, error))
			{
				result.errors.push_back(std::move(error));
				continue;
			}

			song->info.path = path;
			song->info.name = juce::File(path).getFileName().toStdString();
			song->info.durationSeconds = song->events.empty() ? 0.0 : song->events.back().seconds;
			next->songs.push_back(std::move(song));
			++result.added;
		}

		if(result.added)
			publish(std::move(next));
		return result;
	}

	bool MidiPlayer::move(const size_t _from, size_t _to)
	{
		auto current = std::atomic_load_explicit(&m_playlist, std::memory_order_acquire);
		if(!current || _from >= current->songs.size() || _to > current->songs.size())
			return false;
		if(_to == _from || _to == _from + 1)
			return true;

		auto next = std::make_shared<Playlist>(*current);
		auto song = next->songs[_from];
		next->songs.erase(next->songs.begin() + static_cast<std::ptrdiff_t>(_from));
		if(_to > _from)
			--_to;
		next->songs.insert(next->songs.begin() + static_cast<std::ptrdiff_t>(_to), std::move(song));
		publish(std::move(next));
		return true;
	}

	bool MidiPlayer::remove(const size_t _index)
	{
		auto current = std::atomic_load_explicit(&m_playlist, std::memory_order_acquire);
		if(!current || _index >= current->songs.size())
			return false;

		auto next = std::make_shared<Playlist>(*current);
		next->songs.erase(next->songs.begin() + static_cast<std::ptrdiff_t>(_index));
		publish(std::move(next));
		return true;
	}

	bool MidiPlayer::clear()
	{
		auto current = std::atomic_load_explicit(&m_playlist, std::memory_order_acquire);
		if(!current || current->songs.empty())
			return false;

		publish(std::make_shared<Playlist>());
		return true;
	}

	void MidiPlayer::play(const size_t _index)
	{
		post(Command::Play, _index);
	}

	void MidiPlayer::togglePlayPause()
	{
		post(Command::Toggle);
	}

	void MidiPlayer::stop()
	{
		post(Command::Stop);
	}

	std::vector<MidiPlayer::Entry> MidiPlayer::entries() const
	{
		std::vector<Entry> result;
		const auto playlist = std::atomic_load_explicit(&m_playlist, std::memory_order_acquire);
		if(!playlist)
			return result;
		result.reserve(playlist->songs.size());
		for(const auto& song : playlist->songs)
			result.push_back(song->info);
		return result;
	}

	uint64_t MidiPlayer::playlistRevision() const
	{
		const auto playlist = std::atomic_load_explicit(&m_playlist, std::memory_order_acquire);
		return playlist ? playlist->revision : 0;
	}

	MidiPlayer::Status MidiPlayer::status() const
	{
		Status result;
		result.state = static_cast<State>(m_state.load(std::memory_order_acquire));
		result.currentIndex = m_currentIndex.load(std::memory_order_acquire);
		result.positionSeconds = static_cast<double>(m_positionMicros.load(std::memory_order_acquire)) * 1.0e-6;
		result.revision = m_statusRevision.load(std::memory_order_acquire);
		return result;
	}

	void MidiPlayer::publish(std::shared_ptr<Playlist> _playlist)
	{
		const auto current = std::atomic_load_explicit(&m_playlist, std::memory_order_acquire);
		_playlist->revision = current ? current->revision + 1 : 1;
		std::atomic_store_explicit(&m_playlist, std::shared_ptr<const Playlist>(std::move(_playlist)),
		                           std::memory_order_release);
	}

	void MidiPlayer::post(const Command _command, const size_t _index)
	{
		const auto generation = m_nextCommandGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
		const auto packed = (static_cast<uint64_t>(generation) << 32) |
		                    ((static_cast<uint64_t>(_index) & commandIndexMask) << 8) |
		                    static_cast<uint8_t>(_command);
		m_command.store(packed, std::memory_order_release);
	}

	void MidiPlayer::processBlock(std::vector<synthLib::SMidiEvent>& _events,
	                              const uint32_t _sampleCount, const double _sampleRate)
	{
		if(!_sampleCount || _sampleRate <= 0.0)
			return;

		const auto published = std::atomic_load_explicit(&m_playlist, std::memory_order_acquire);
		if(published != m_audioPlaylist)
		{
			m_audioPlaylist = published;
			if(m_audioSong)
			{
				const auto found = std::find(m_audioPlaylist->songs.begin(), m_audioPlaylist->songs.end(), m_audioSong);
				if(found == m_audioPlaylist->songs.end())
				{
					silence(0, _events);
					m_audioSong.reset();
					m_audioIndex = -1;
					m_audioState = State::Stopped;
					m_cursorSamples = 0;
					m_eventIndex = 0;
				}
				else
					m_audioIndex = static_cast<int>(std::distance(m_audioPlaylist->songs.begin(), found));
			}
		}

		if(m_lastSampleRate > 0.0 && std::abs(m_lastSampleRate - _sampleRate) > 0.01)
		{
			const auto seconds = static_cast<double>(m_cursorSamples) / m_lastSampleRate;
			m_cursorSamples = static_cast<uint64_t>(std::llround(seconds * _sampleRate));
		}
		m_lastSampleRate = _sampleRate;

		const auto command = m_command.load(std::memory_order_acquire);
		if(command != m_lastCommand)
		{
			m_lastCommand = command;
			applyCommand(command, _events);
		}

		if(m_audioState != State::Playing || !m_audioSong)
		{
			updatePublishedStatus();
			return;
		}

		uint32_t outputOffset = 0;
		uint32_t remaining = _sampleCount;
		while(remaining && m_audioState == State::Playing && m_audioSong)
		{
			const auto endSample = std::max<uint64_t>(1,
				static_cast<uint64_t>(std::ceil(
					(m_audioSong->info.durationSeconds + kEndTailSeconds) * _sampleRate)) + 1);
			const auto blockEnd = m_cursorSamples + remaining;

			while(m_eventIndex < m_audioSong->events.size())
			{
				const auto& source = m_audioSong->events[m_eventIndex];
				const auto eventSample = static_cast<uint64_t>(std::llround(source.seconds * _sampleRate));
				if(eventSample >= blockEnd)
					break;
				++m_eventIndex;
				if(eventSample < m_cursorSamples || source.bytes.empty())
					continue;

				synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
				event.offset = outputOffset + static_cast<uint32_t>(eventSample - m_cursorSamples);
				event.port = source.port;
				if(source.bytes.front() == 0xf0)
				{
					// SysexBuffer is a std::pmr::vector wherever pmr is available, so it
					// cannot be assigned from the file parser's plain std::vector. The event
					// is freshly constructed here, so replacing is the right semantics.
					event.sysex.assign(source.bytes.begin(), source.bytes.end());
					if(event.sysex.back() != 0xf7)
						event.sysex.push_back(0xf7);
				}
				else if(source.bytes.size() <= 3)
				{
					event.a = source.bytes[0];
					event.b = source.bytes.size() > 1 ? source.bytes[1] : 0;
					event.c = source.bytes.size() > 2 ? source.bytes[2] : 0;
				}
				else
					continue;
				_events.push_back(std::move(event));
			}

			const auto samplesToEnd = endSample > m_cursorSamples ? endSample - m_cursorSamples : 0;
			if(samplesToEnd >= remaining)
			{
				m_cursorSamples += remaining;
				remaining = 0;
				break;
			}

			outputOffset += static_cast<uint32_t>(samplesToEnd);
			remaining -= static_cast<uint32_t>(samplesToEnd);
			const auto nextIndex = m_audioIndex + 1;
			if(!m_audioPlaylist || nextIndex < 0 ||
			   static_cast<size_t>(nextIndex) >= m_audioPlaylist->songs.size())
			{
				m_audioState = State::Stopped;
				silence(outputOffset, _events);
				m_cursorSamples = 0;
				m_eventIndex = 0;
				break;
			}
			selectSong(static_cast<size_t>(nextIndex), true, outputOffset, _events);
		}

		updatePublishedStatus();
	}

	void MidiPlayer::applyCommand(const uint64_t _packed,
	                              std::vector<synthLib::SMidiEvent>& _events)
	{
		const auto command = static_cast<Command>(_packed & 0xffu);
		const auto index = static_cast<size_t>((_packed >> 8) & commandIndexMask);
		switch(command)
		{
		case Command::Play:
			selectSong(index, true, 0, _events);
			break;
		case Command::Toggle:
			if(m_audioState == State::Playing)
			{
				m_audioState = State::Paused;
				silence(0, _events);
			}
			else if(m_audioSong)
				m_audioState = State::Playing;
			else if(m_audioPlaylist && !m_audioPlaylist->songs.empty())
				selectSong(0, true, 0, _events);
			break;
		case Command::Stop:
			silence(0, _events);
			m_audioState = State::Stopped;
			m_cursorSamples = 0;
			m_eventIndex = 0;
			break;
		case Command::None:
			break;
		}
	}

	void MidiPlayer::selectSong(const size_t _index, const bool _play, const uint32_t _offset,
	                            std::vector<synthLib::SMidiEvent>& _events)
	{
		if(!m_audioPlaylist || _index >= m_audioPlaylist->songs.size())
			return;
		if(m_audioSong)
			silence(_offset, _events);
		m_audioSong = m_audioPlaylist->songs[_index];
		m_audioIndex = static_cast<int>(_index);
		m_cursorSamples = 0;
		m_eventIndex = 0;
		m_audioState = _play ? State::Playing : State::Stopped;
	}

	void MidiPlayer::silence(const uint32_t _offset,
	                        std::vector<synthLib::SMidiEvent>& _events) const
	{
		// All four logical ports, not just A/B: the SC-8850 receives on four USB
		// cables and a file can place notes on any of them. Boards with fewer
		// inputs fold the higher ports onto the ones they have, so covering all
		// four is harmless there and is the only way C/D ever go quiet.
		for(uint8_t port = 0; port < kPortCount; ++port)
			for(uint8_t channel = 0; channel < 16; ++channel)
			{
				_events.emplace_back(synthLib::MidiEventSource::Host,
				                     static_cast<uint8_t>(0xb0 | channel), 120, 0, _offset);
				_events.back().port = port;
				_events.emplace_back(synthLib::MidiEventSource::Host,
				                     static_cast<uint8_t>(0xb0 | channel), 123, 0, _offset);
				_events.back().port = port;
			}
	}

	void MidiPlayer::updatePublishedStatus()
	{
		const auto state = static_cast<uint8_t>(m_audioState);
		const auto micros = m_lastSampleRate > 0.0
			? static_cast<uint64_t>(std::llround(static_cast<double>(m_cursorSamples) * 1.0e6 / m_lastSampleRate))
			: 0;
		const bool stateChanged = m_state.exchange(state, std::memory_order_release) != state;
		const bool indexChanged = m_currentIndex.exchange(m_audioIndex, std::memory_order_release) != m_audioIndex;
		m_positionMicros.store(micros, std::memory_order_release);
		if(stateChanged || indexChanged)
			m_statusRevision.fetch_add(1, std::memory_order_release);
	}
}
