#pragma once

#include <bitset>
#include <cstdint>
#include <random>
#include <vector>

#include "midiTypes.h"

namespace synthLib
{
	struct ArpStep
	{
		bool active = true;				// false makes the step a rest. It takes the note it would have played
		uint8_t velocity = 0;			// 0 plays the velocity the key was struck with
		uint8_t releaseVelocity = 64;	// the note off velocity. 64 is what MIDI defines for a note off without one
		float length = 0.5f;			// in steps: 1 is legato for a step of duration 1, more overlaps what follows
		float duration = 1.0f;			// in steps, until the next step starts: 0.5 for a ratchet, 2/3 for a triplet
		int8_t velocityOffset = 0;		// added to the velocity
	};

	// The length of a step as a fraction of a whole note: 1/16 is {1, 16}, a dotted eighth {3, 16}, an eighth triplet
	// {1, 12}
	struct ArpDivision
	{
		uint16_t numerator = 1;
		uint16_t denominator = 16;
	};

	enum class ArpMode : uint8_t
	{
		Off,
		Up,
		Down,
		UpDown,		// the top and the bottom note are played once per cycle
		AsPlayed,
		Random,
		Chord		// all held notes on every step, one octave higher per step through the range
	};

	struct ArpSettings
	{
		ArpMode mode = ArpMode::Off;
		uint8_t channel = 0;			// the notes on this channel are arpeggiated, and the arpeggio plays on it
		uint8_t octaves = 1;			// 1 plays the held notes only
		bool hold = false;				// notes stay when their keys are released, until a key is struck with none down
		bool holdRestarts = false;		// that key starts the pattern over, as a first key does, and ends the notes
										// that still sound
		bool sync = true;				// steps sit on the host's song position while it plays, and keep its tempo
										// while it is stopped. The pattern starts over with the arpeggio
		float startGrid = 1.0f;			// the arpeggio starts on a grid of this share of a step
		float lateStart = 0.25f;		// a key struck this late after a point of that grid, as a share of its spacing,
										// still plays at once, and the step after it. A later one waits for the next
										// point
		bool lateStartPlaysKey = false;	// that note is the key itself, with its velocity, and the arpeggio goes on
										// after it
		bool asPlayedFromSecond = false;	// As Played starts with the second note
		float tempo = 120.0f;			// in BPM, used when not synced, or when the host reports no tempo
		ArpDivision division;
		float swing = 0.5f;				// the share of a pair of steps that the first one takes: 0.5 is straight,
										// 0.75 dotted. Positions inside the pair are stretched and squeezed with it
		float noteLength = 1.0f;		// scales the length of every step
		std::vector<ArpStep> pattern;	// played in a loop. Empty plays every step with the defaults of ArpStep
	};

	// Arpeggiates in C++ for devices whose firmware arpeggiator cannot follow the host exactly: the firmware only sees
	// MIDI clock, and has to guess tempo and position from it. This one reads the host's song position and places every
	// step on the sample it belongs to.
	class Arpeggiator
	{
	public:
		Arpeggiator();

		// Takes effect with the next process(). Notes that play keep playing, unless the mode is switched off or the
		// channel changes
		void setSettings(const ArpSettings& _settings);
		const ArpSettings& getSettings() const { return m_settings; }

		// Processes one block. Takes the notes of its channel out of _midiIn, whose offsets count from the block start,
		// and appends what it plays to _out, in time order. A note off whose note on reached the device while the
		// arpeggiator was off stays in _midiIn, so that the device does not keep the note forever.
		// The host's transport comes as MidiEventType::HostTransport events in _midiIn, they stay there. Between them,
		// and without any, the arpeggio runs on at the tempo it had last.
		void process(std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _out, uint32_t _sampleCount,
			float _samplerate);

	private:
		struct Note
		{
			uint8_t note;
			uint8_t velocity;
		};

		struct PlayingNote
		{
			uint8_t note;
			uint8_t releaseVelocity;
			uint64_t offTime;
		};

		void applySettings(std::vector<SMidiEvent>& _out);
		void trackPassThrough(const std::vector<SMidiEvent>& _midiIn);
		void takeInput(std::vector<SMidiEvent>& _midiIn);
		void handleInput(const SMidiEvent& _ev, std::vector<SMidiEvent>& _out, uint32_t _offset);
		void handleTransport(const HostTransport& _transport, uint32_t _offset);
		void relocate(uint32_t _offset);
		double tempo() const;
		void start(uint32_t _offset);
		void playStep(std::vector<SMidiEvent>& _out, uint32_t _offset);
		void playNote(std::vector<SMidiEvent>& _out, uint32_t _offset, uint32_t _note, uint8_t _velocity,
			uint8_t _releaseVelocity, uint64_t _length);
		void joinChord(std::vector<SMidiEvent>& _out, uint32_t _offset, uint8_t _note, uint8_t _velocity);
		void endNotes(std::vector<SMidiEvent>& _out, uint32_t _offset);
		void endNote(std::vector<SMidiEvent>& _out, uint32_t _offset, const PlayingNote& _note) const;
		void stopAll(std::vector<SMidiEvent>& _out, uint32_t _offset);
		void buildSequence();
		const Note* nextNote();

		bool canStep() const { return m_stepLength > 0.0 && m_quartersPerSample > 0.0 && m_cycleLength > 0.0; }
		uint32_t nextNoteOffOffset(uint32_t _from) const;
		uint32_t stepOffset(int64_t _step, uint32_t _from) const;
		double nominalPos(int64_t _step) const;
		double stepPos(int64_t _step) const;
		int64_t firstStepFrom(double _pos) const;
		double warp(double _pos) const;
		double unwarp(double _pos) const;

		bool isOurs(const SMidiEvent& _ev) const;

		ArpSettings m_settings;
		bool m_settingsChanged = true;

		uint8_t m_channel = 0;				// the channel the notes that play now were started on

		std::vector<Note> m_held;			// in the order they were struck
		std::bitset<128> m_keysDown;
		std::bitset<128> m_passThrough;		// note ons that reached the device while the arpeggiator was off
		std::vector<Note> m_sequence;		// the notes in the order the mode plays them, octaves included
		std::vector<Note> m_previous;		// m_sequence before it changed
		bool m_sequenceDirty = true;
		uint32_t m_noteIndex = 0;			// counts the steps played, Chord takes its octave from it
		int m_lastNote = -1;				// what the last step played, the next step continues from it
		size_t m_lastIndex = 0;				// As Played: where m_lastNote is in m_sequence
		bool m_goingDown = false;			// Up&Down
		std::minstd_rand m_random;

		std::vector<PlayingNote> m_playing;
		ArpStep m_chordStep;				// Chord: the step that sounds, until m_chordEnd, a key struck joins it
		int m_chordTranspose = 0;
		uint64_t m_chordEnd = 0;
		uint64_t m_time = 0;				// in samples, counted over all blocks

		bool m_running = false;
		Note m_startKey{};					// the key that started the arpeggio
		int64_t m_nextStep = 0;
		double m_lastStepTime = -1e18;		// in samples, like m_time
		bool m_relocate = true;
		bool m_wasSynced = false;
		float m_hostBpm = 0.0f;				// the last tempo the host reported, 0 if none
		float m_samplerate = 0.0f;
		double m_stepLength = 0.25;			// in quarter notes
		double m_origin = 0.0;				// where the pattern started, in quarter notes like m_blockPos
		double m_swing = 0.5;
		std::vector<double> m_stepStarts;	// where each step of the pattern starts, in steps, and its total length
		double m_cycleLength = 1.0;			// of the pattern, in steps
		double m_blockPos = 0.0;			// the position of the block start, in quarter notes
		double m_quartersPerSample = 0.0;
		double m_nextBlockPos = 0.0;

		std::vector<SMidiEvent> m_input;
	};
}
