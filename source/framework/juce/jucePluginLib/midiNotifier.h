#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "baseLib/event.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace pluginLib
{
	// Surfaces live note events to anyone who wants to draw them - a keyboard in a skin, an activity
	// indicator. Notes arrive on the audio thread and are handed on via the message thread, so a
	// listener never has to think about which thread it is on.
	//
	// Nothing is dispatched while nobody listens, which is the normal case: a skin that does not ask
	// for notes costs one atomic read per event.
	//
	// Pitch bend and channel pressure are not dispatched at all, a wheel produces them by the
	// thousand. The last value of each channel is kept instead, for a skin to read when it draws.
	class MidiNotifier
	{
	public:
		MidiNotifier();
		~MidiNotifier();

		MidiNotifier(const MidiNotifier&) = delete;
		MidiNotifier& operator=(const MidiNotifier&) = delete;

		struct Note
		{
			uint8_t note = 0;
			uint8_t velocity = 0;		// zero for a note off, including a note on that means one
			uint8_t channel = 0;		// 1-16, as a musician counts them
			bool on = false;
		};

		baseLib::Event<Note> evNote;

		using ListenerId = baseLib::Event<Note>::ListenerId;

		// any thread
		void onMidiEvent(const synthLib::SMidiEvent& _ev);

		// false when the event is not a note at all
		static bool toNote(const synthLib::SMidiEvent& _ev, Note& _note);

		// message thread
		ListenerId addNoteListener(const baseLib::Event<Note>::Callback& _callback);
		void removeNoteListener(ListenerId _id);

		// Any thread, the channel counts from 1 like Note::channel. Pitch bend -8192 to 8191, 0 is the centre,
		// pressure 0-127. Reset All Controllers returns both to zero, as it does on the synth. Only what the device
		// receives counts, not what it sends
		int getPitchBend(const uint8_t _channel) const { return m_pitchBend[(_channel - 1) & 0x0f].load(std::memory_order_relaxed); }
		int getChannelPressure(const uint8_t _channel) const { return m_channelPressure[(_channel - 1) & 0x0f].load(std::memory_order_relaxed); }

	private:
		std::atomic<uint32_t> m_noteListenerCount{0};

		std::array<std::atomic<int16_t>, 16> m_pitchBend{};
		std::array<std::atomic<uint8_t>, 16> m_channelPressure{};
	};
}
