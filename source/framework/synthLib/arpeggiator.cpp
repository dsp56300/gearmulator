#include "arpeggiator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace synthLib
{
	namespace
	{
		// The host moved by more than this between two blocks: it located, looped or restarted. The same tolerance as
		// in Plugin::updateTransport
		constexpr double g_discontinuity = 0.01;	// quarter notes

		// A key struck this late into a step, as a share of the step, still plays that step instead of waiting for the
		// next one. Notes recorded on the grid arrive a sample or two after the step start they belong to
		constexpr double g_lateStart = 0.25;

		constexpr uint32_t g_never = std::numeric_limits<uint32_t>::max();

		constexpr uint8_t g_maxOctaves = 10;

		bool isNoteOn(const SMidiEvent& _ev)
		{
			return (_ev.a & 0xf0) == M_NOTEON && _ev.c > 0;
		}

		bool isNoteOff(const SMidiEvent& _ev)
		{
			const auto status = _ev.a & 0xf0;
			return status == M_NOTEOFF || (status == M_NOTEON && _ev.c == 0);
		}

		bool isAllNotesOff(const SMidiEvent& _ev)
		{
			return _ev.type == MidiEventType::Midi && _ev.sysex.empty() && (_ev.a & 0xf0) == M_CONTROLCHANGE &&
				(_ev.b == MC_ALLNOTESOFF || _ev.b == MC_ALLSOUNDOFF);
		}
	}

	Arpeggiator::Arpeggiator()
	{
		// process() runs on the audio thread, nothing may grow there. A pattern longer than 256 steps is the exception
		m_held.reserve(128);
		m_sequence.reserve(128 * g_maxOctaves);
		m_playing.reserve(128);
		m_input.reserve(128);
		m_stepStarts.reserve(257);
	}

	void Arpeggiator::setSettings(const ArpSettings& _settings)
	{
		m_settings = _settings;
		m_settingsChanged = true;
	}

	void Arpeggiator::process(std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _out,
		const uint32_t _sampleCount, const float _samplerate)
	{
		applySettings(_out);

		if (m_settings.mode == ArpMode::Off)
		{
			trackPassThrough(_midiIn);
			m_time += _sampleCount;
			m_wasSynced = false;
			return;
		}

		// Runs on from where the last block ended, at the tempo it had, until the host's transport says otherwise. That
		// also keeps the arpeggio from stumbling when the host stops
		m_samplerate = _samplerate;
		m_blockPos = m_nextBlockPos;
		m_quartersPerSample = _samplerate > 0.0f ? tempo() / (60.0 * _samplerate) : 0.0;

		if (m_relocate && m_running && canStep())
			relocate(0);

		m_relocate = false;

		takeInput(_midiIn);

		size_t input = 0;
		uint32_t t = 0;

		while (true)
		{
			const uint32_t tInput = input < m_input.size() ? std::max(t, m_input[input].offset) : g_never;
			const uint32_t tOff = nextNoteOffOffset(t);
			const uint32_t tStep = m_running && canStep() ? stepOffset(m_nextStep, t) : g_never;

			const uint32_t tNext = std::min({tInput, tOff, tStep});

			if (tNext >= _sampleCount)
				break;

			t = tNext;

			// At the same sample, notes end before new ones begin, and keys are taken before the step that may play
			// them
			if (tOff == t)
			{
				endNotes(_out, t);
			}
			else if (tInput == t)
			{
				handleInput(m_input[input++], _out, t);
			}
			else
			{
				playStep(_out, t);
				++m_nextStep;
				m_lastStepTime = static_cast<double>(m_time + t);
			}
		}

		// Events at or past the end of the block still change which keys are held. The next block plays the steps
		// they start
		for (; input < m_input.size(); ++input)
			handleInput(m_input[input], _out, _sampleCount ? _sampleCount - 1 : 0);

		m_time += _sampleCount;
		m_nextBlockPos = m_blockPos + _sampleCount * m_quartersPerSample;
	}

	void Arpeggiator::handleTransport(const HostTransport& _transport, const uint32_t _offset)
	{
		const auto pos = m_blockPos + _offset * m_quartersPerSample;

		m_hostBpm = _transport.bpm;
		const auto rate = m_samplerate > 0.0f ? tempo() / (60.0 * m_samplerate) : 0.0;

		const bool synced = m_settings.sync && m_hostBpm > 0.0f && _transport.isPlaying && _transport.hasPpqPosition;

		// The host's song position wins. When it jumped, the host located or looped, or it just started to play, the
		// next step is the one the new position wants. Otherwise the arpeggio runs on from where it is
		const bool jumped = synced && (!m_wasSynced || std::fabs(_transport.ppqPos - pos) > g_discontinuity);

		m_blockPos = (synced ? _transport.ppqPos : pos) - _offset * rate;
		m_quartersPerSample = rate;
		m_wasSynced = synced;

		if (jumped && m_running && canStep())
			relocate(_offset);
	}

	void Arpeggiator::relocate(const uint32_t _offset)
	{
		// A step half a sample before now rounds to now
		const auto pos = m_blockPos + _offset * m_quartersPerSample;
		m_nextStep = firstStepFrom(pos - 0.5 * m_quartersPerSample);

		// A host that wraps its loop late reports the loop start after the step at the loop end has played. The two are
		// the same step, playing it again would flam. Half of the step's duration tells the two cases apart
		const auto sinceLastStep = static_cast<double>(m_time + stepOffset(m_nextStep, _offset)) - m_lastStepTime;
		const auto duration = stepPos(m_nextStep + 1) - stepPos(m_nextStep);
		if (sinceLastStep < 0.5 * duration / m_quartersPerSample)
			++m_nextStep;
	}

	double Arpeggiator::tempo() const
	{
		return m_settings.sync && m_hostBpm > 0.0f ? m_hostBpm : m_settings.tempo;
	}

	void Arpeggiator::applySettings(std::vector<SMidiEvent>& _out)
	{
		if (!m_settingsChanged)
			return;

		m_settingsChanged = false;

		if (m_settings.mode == ArpMode::Off || m_settings.channel != m_channel)
		{
			stopAll(_out, 0);

			if (m_settings.channel != m_channel)
			{
				m_passThrough.reset();
				m_channel = m_settings.channel;
			}
		}
		else if (!m_settings.hold)
		{
			// The notes whose keys are up only stayed because of hold
			m_held.erase(std::remove_if(m_held.begin(), m_held.end(), [this](const Note& _n)
			{
				return !m_keysDown.test(_n.note);
			}), m_held.end());

			if (m_held.empty())
				m_running = false;
		}

		const auto& division = m_settings.division;
		m_stepLength = division.denominator ? 4.0 * division.numerator / division.denominator : 0.0;
		m_swing = std::clamp(m_settings.swing, 0.5f, 0.95f);

		// No pattern plays every step with the defaults of ArpStep
		const auto& pattern = m_settings.pattern;
		m_stepStarts.assign(1, 0.0);
		for (size_t i = 0; i < std::max<size_t>(pattern.size(), 1); ++i)
		{
			const auto duration = pattern.empty() ? ArpStep().duration : pattern[i].duration;
			m_stepStarts.push_back(m_stepStarts.back() + std::max(0.0, static_cast<double>(duration)));
		}
		m_cycleLength = m_stepStarts.back();

		// The steps may sit elsewhere now
		m_relocate = true;
		m_sequenceDirty = true;
	}

	void Arpeggiator::trackPassThrough(const std::vector<SMidiEvent>& _midiIn)
	{
		for (const auto& ev : _midiIn)
		{
			if (isOurs(ev))
				m_passThrough.set(ev.b, isNoteOn(ev));
		}
	}

	void Arpeggiator::takeInput(std::vector<SMidiEvent>& _midiIn)
	{
		m_input.clear();

		auto keep = _midiIn.begin();

		for (auto it = _midiIn.begin(); it != _midiIn.end(); ++it)
		{
			if (isOurs(*it))
			{
				m_input.push_back(*it);

				// The device got the note on before the arpeggiator was switched on, so it gets the note off too
				if (!isNoteOff(*it) || !m_passThrough.test(it->b))
					continue;

				m_passThrough.reset(it->b);
			}
			else if (it->type == MidiEventType::HostTransport || (isAllNotesOff(*it) && (it->a & 0x0f) == m_channel))
			{
				m_input.push_back(*it);
			}

			if (keep != it)
				*keep = std::move(*it);
			++keep;
		}

		_midiIn.erase(keep, _midiIn.end());
	}

	void Arpeggiator::handleInput(const SMidiEvent& _ev, std::vector<SMidiEvent>& _out, const uint32_t _offset)
	{
		if (_ev.type == MidiEventType::HostTransport)
		{
			handleTransport(_ev.transport, _offset);
			return;
		}

		if (isAllNotesOff(_ev))
		{
			stopAll(_out, _offset);
			return;
		}

		const auto note = _ev.b;
		const auto held = std::find_if(m_held.begin(), m_held.end(), [&](const Note& _n) { return _n.note == note; });

		if (isNoteOn(_ev))
		{
			// With hold, a key struck while no key is down starts a new set of notes
			if (m_settings.hold && m_keysDown.none() && !m_held.empty())
			{
				m_held.clear();
				m_noteIndex = 0;
				m_lastNote = -1;
				m_goingDown = false;
				m_held.push_back({note, _ev.c});
			}
			else if (held != m_held.end())
			{
				held->velocity = _ev.c;
			}
			else
			{
				m_held.push_back({note, _ev.c});
			}

			m_keysDown.set(note);
			m_sequenceDirty = true;

			if (!m_running)
				start(_offset);
			return;
		}

		m_keysDown.reset(note);

		if (m_settings.hold || held == m_held.end())
			return;

		m_held.erase(held);
		m_sequenceDirty = true;

		// Notes that play now play to their end
		if (m_held.empty())
			m_running = false;
	}

	void Arpeggiator::start(const uint32_t _offset)
	{
		m_running = true;
		m_noteIndex = 0;
		m_lastNote = -1;
		m_goingDown = false;

		if (!canStep())
			return;

		const double pos = m_blockPos + _offset * m_quartersPerSample;

		if (!m_wasSynced)
		{
			// Without the host's song position, the arpeggio starts with the key
			m_blockPos -= pos;
			m_nextStep = 0;
			return;
		}

		const double halfSample = 0.5 * m_quartersPerSample;
		const auto next = firstStepFrom(pos - halfSample);

		// A step on this sample plays now. Otherwise, the step the key falls into plays right away if the key is only a
		// little late for it, and if it is not, the arpeggio starts with the next step
		const auto prev = stepPos(next - 1);

		if (stepPos(next) - pos < halfSample)
			m_nextStep = next;
		else
			m_nextStep = pos - prev <= g_lateStart * (stepPos(next) - prev) ? next - 1 : next;
	}

	void Arpeggiator::playStep(std::vector<SMidiEvent>& _out, const uint32_t _offset)
	{
		if (m_sequenceDirty)
			buildSequence();

		if (m_sequence.empty())
			return;

		const auto& pattern = m_settings.pattern;
		const auto patternSize = static_cast<int64_t>(pattern.size());
		const auto patternStep = patternSize ? (m_nextStep % patternSize + patternSize) % patternSize : 0;
		const ArpStep step = patternSize ? pattern[static_cast<size_t>(patternStep)] : ArpStep();

		if (!step.active)
			return;

		// The note ends where the step position plus its length lands once swing has moved both
		const auto pos = nominalPos(m_nextStep);
		const auto end = warp(pos + static_cast<double>(step.length) * m_settings.noteLength) * m_stepLength;
		const auto length = static_cast<uint64_t>(std::max(1.0, std::round((end - stepPos(m_nextStep)) / m_quartersPerSample)));

		auto velocity = [&](const Note& _n)
		{
			const int v = (step.velocity ? step.velocity : _n.velocity) + step.velocityOffset;
			return static_cast<uint8_t>(std::clamp(v, 1, 127));
		};

		if (m_settings.mode == ArpMode::Chord)
		{
			const auto transpose = 12 * (m_noteIndex % std::clamp<uint8_t>(m_settings.octaves, 1, g_maxOctaves));

			for (const auto& n : m_sequence)
				playNote(_out, _offset, n.note + transpose, velocity(n), step.releaseVelocity, length);
		}
		else if (const auto* n = nextNote())
		{
			m_lastNote = n->note;
			playNote(_out, _offset, n->note, velocity(*n), step.releaseVelocity, length);
		}

		++m_noteIndex;
	}

	const Arpeggiator::Note* Arpeggiator::nextNote()
	{
		// m_sequence is ascending for all modes but As Played and Down. A mode moves on from the note the last step
		// played, so that a key struck while the arpeggio runs joins it where it fits instead of shifting it
		const auto begin = m_sequence.begin();
		const auto end = m_sequence.end();

		auto higher = [&] { return std::find_if(begin, end, [&](const Note& _n) { return _n.note > m_lastNote; }); };
		auto lower = [&]
		{
			const auto it = std::find_if(m_sequence.rbegin(), m_sequence.rend(), [&](const Note& _n) { return _n.note < m_lastNote; });
			return it == m_sequence.rend() ? end : std::prev(it.base());
		};

		switch (m_settings.mode)
		{
		case ArpMode::Up:
			{
				const auto it = higher();
				return it != end ? &*it : &m_sequence.front();
			}
		case ArpMode::Down:
			{
				// descending, see buildSequence
				const auto it = m_lastNote < 0 ? begin : std::find_if(begin, end, [&](const Note& _n) { return _n.note < m_lastNote; });
				return it != end ? &*it : &m_sequence.front();
			}
		case ArpMode::UpDown:
			{
				// The top and the bottom note turn the direction and play once
				auto it = m_goingDown ? lower() : higher();
				if (it == end)
				{
					m_goingDown = !m_goingDown;
					it = m_goingDown ? lower() : higher();
				}
				return it != end ? &*it : &m_sequence.front();
			}
		case ArpMode::AsPlayed:
			{
				// The same note can be in the sequence twice, a key and another one's octave. Where the last note was
				// is kept, and only looked up when the sequence changed under it
				const auto size = m_sequence.size();
				if (m_lastNote < 0)
				{
					m_lastIndex = 0;
					return &m_sequence.front();
				}
				if (m_lastIndex >= size || m_sequence[m_lastIndex].note != m_lastNote)
				{
					const auto it = std::find_if(begin, end, [&](const Note& _n) { return _n.note == m_lastNote; });
					if (it != end)
						m_lastIndex = static_cast<size_t>(it - begin);
				}
				m_lastIndex = (m_lastIndex + 1) % size;
				return &m_sequence[m_lastIndex];
			}
		case ArpMode::Random:
			return &m_sequence[m_random() % m_sequence.size()];
		default:
			return nullptr;
		}
	}

	void Arpeggiator::playNote(std::vector<SMidiEvent>& _out, const uint32_t _offset, const uint32_t _note,
		const uint8_t _velocity, const uint8_t _releaseVelocity, const uint64_t _length)
	{
		if (_note > 127)
			return;

		// The same note may still sound from an earlier step. Ending it first keeps the device's note ons and offs in
		// pairs
		const auto playing = std::find_if(m_playing.begin(), m_playing.end(), [&](const PlayingNote& _p)
		{
			return _p.note == _note;
		});

		if (playing != m_playing.end())
		{
			endNote(_out, _offset, *playing);
			m_playing.erase(playing);
		}

		const auto note = static_cast<uint8_t>(_note);
		const auto status = static_cast<uint8_t>(M_NOTEON | m_channel);

		_out.emplace_back(MidiEventSource::Internal, status, note, std::min<uint8_t>(_velocity, 127), _offset);
		m_playing.push_back({note, _releaseVelocity, m_time + _offset + _length});
	}

	void Arpeggiator::endNotes(std::vector<SMidiEvent>& _out, const uint32_t _offset)
	{
		const auto now = m_time + _offset;

		auto keep = m_playing.begin();

		for (auto it = m_playing.begin(); it != m_playing.end(); ++it)
		{
			if (it->offTime <= now)
				endNote(_out, _offset, *it);
			else
				*keep++ = *it;
		}

		m_playing.erase(keep, m_playing.end());
	}

	void Arpeggiator::endNote(std::vector<SMidiEvent>& _out, const uint32_t _offset, const PlayingNote& _note) const
	{
		const auto status = static_cast<uint8_t>(M_NOTEOFF | m_channel);
		const auto velocity = std::min<uint8_t>(_note.releaseVelocity, 127);
		_out.emplace_back(MidiEventSource::Internal, status, _note.note, velocity, _offset);
	}

	void Arpeggiator::stopAll(std::vector<SMidiEvent>& _out, const uint32_t _offset)
	{
		for (const auto& n : m_playing)
			endNote(_out, _offset, n);

		m_playing.clear();
		m_held.clear();
		m_keysDown.reset();
		m_sequenceDirty = true;
		m_running = false;
	}

	void Arpeggiator::buildSequence()
	{
		m_sequenceDirty = false;

		const auto mode = m_settings.mode;

		m_sequence.assign(m_held.begin(), m_held.end());

		if (mode != ArpMode::AsPlayed)
		{
			std::sort(m_sequence.begin(), m_sequence.end(), [](const Note& _a, const Note& _b)
			{
				return _a.note < _b.note;
			});
		}

		// Chord moves the whole chord through the octaves, one per step
		if (mode == ArpMode::Chord)
			return;

		const auto count = m_sequence.size();
		const auto octaves = std::clamp<uint8_t>(m_settings.octaves, 1, g_maxOctaves);

		for (uint32_t o = 1; o < octaves; ++o)
		{
			for (size_t i = 0; i < count; ++i)
			{
				const auto note = m_sequence[i].note + 12 * o;

				if (note <= 127)
					m_sequence.push_back({static_cast<uint8_t>(note), m_sequence[i].velocity});
			}
		}

		if (mode == ArpMode::Down)
			std::reverse(m_sequence.begin(), m_sequence.end());
	}

	uint32_t Arpeggiator::nextNoteOffOffset(const uint32_t _from) const
	{
		if (m_playing.empty())
			return g_never;

		uint64_t next = std::numeric_limits<uint64_t>::max();

		for (const auto& p : m_playing)
			next = std::min(next, p.offTime);

		if (next <= m_time + _from)
			return _from;

		return static_cast<uint32_t>(std::min<uint64_t>(next - m_time, g_never));
	}

	uint32_t Arpeggiator::stepOffset(const int64_t _step, const uint32_t _from) const
	{
		// Rounded to the nearest sample. A step between two blocks rounds into one of them, and the other one finds it
		// played or not yet due
		const double offset = std::floor((stepPos(_step) - m_blockPos) / m_quartersPerSample + 0.5);

		if (offset <= _from)
			return _from;

		return offset >= g_never ? g_never : static_cast<uint32_t>(offset);
	}

	double Arpeggiator::nominalPos(const int64_t _step) const
	{
		// in steps, without swing. Step k is step k mod n of the pattern, in cycle k / n
		const auto n = static_cast<int64_t>(m_stepStarts.size() - 1);
		const auto cycle = _step >= 0 ? _step / n : -((-_step + n - 1) / n);
		return static_cast<double>(cycle) * m_cycleLength + m_stepStarts[static_cast<size_t>(_step - cycle * n)];
	}

	double Arpeggiator::stepPos(const int64_t _step) const
	{
		return warp(nominalPos(_step)) * m_stepLength;
	}

	int64_t Arpeggiator::firstStepFrom(const double _pos) const
	{
		// Estimate from the pattern, then settle the rounding
		const auto n = static_cast<int64_t>(m_stepStarts.size() - 1);
		const auto nominal = unwarp(_pos / m_stepLength);
		const auto cycle = std::floor(nominal / m_cycleLength);
		const auto inCycle = nominal - cycle * m_cycleLength;
		const auto first = std::lower_bound(m_stepStarts.begin(), m_stepStarts.end() - 1, inCycle) - m_stepStarts.begin();

		auto step = static_cast<int64_t>(cycle) * n + first;

		while (stepPos(step - 1) >= _pos)
			--step;
		while (stepPos(step) < _pos)
			++step;

		return step;
	}

	double Arpeggiator::warp(const double _pos) const
	{
		// Swing stretches the first step of every pair and squeezes the second, and everything between them with it
		const auto pair = 2.0 * std::floor(_pos * 0.5);
		const auto inPair = _pos - pair;
		const auto first = 2.0 * m_swing;
		return pair + (inPair < 1.0 ? inPair * first : first + (inPair - 1.0) * (2.0 - first));
	}

	double Arpeggiator::unwarp(const double _pos) const
	{
		const auto pair = 2.0 * std::floor(_pos * 0.5);
		const auto inPair = _pos - pair;
		const auto first = 2.0 * m_swing;
		return pair + (inPair < first ? inPair / first : 1.0 + (inPair - first) / (2.0 - first));
	}

	bool Arpeggiator::isOurs(const SMidiEvent& _ev) const
	{
		return _ev.type == MidiEventType::Midi && _ev.sysex.empty() && (_ev.a & 0x0f) == m_channel && _ev.b < 128 &&
			(isNoteOn(_ev) || isNoteOff(_ev));
	}
}
