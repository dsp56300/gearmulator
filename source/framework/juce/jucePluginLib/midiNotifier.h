#pragma once

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

	private:
		std::atomic<uint32_t> m_noteListenerCount{0};
	};
}
