#include "midiNotifier.h"

#include <mutex>
#include <set>

#include "synthLib/midiTypes.h"

#include "juce_events/juce_events.h"

namespace pluginLib
{
	namespace
	{
		// A note handed to the message thread outlives the call that posted it, and the plugin can be
		// torn down in between - closing a project while a note is held does exactly that. The
		// callback checks that the notifier is still alive before touching it.
		std::mutex& getInstancesMutex()
		{
			static std::mutex m;
			return m;
		}

		std::set<MidiNotifier*>& getInstances()
		{
			static std::set<MidiNotifier*> instances;
			return instances;
		}
	}

	MidiNotifier::MidiNotifier()
	{
		std::lock_guard lock(getInstancesMutex());
		getInstances().insert(this);
	}

	MidiNotifier::~MidiNotifier()
	{
		std::lock_guard lock(getInstancesMutex());
		getInstances().erase(this);
	}

	void MidiNotifier::onMidiEvent(const synthLib::SMidiEvent& _ev)
	{
		if (m_noteListenerCount.load(std::memory_order_relaxed) == 0)
			return;

		Note note;

		if (!toNote(_ev, note))
			return;

		// this runs on whichever thread the midi arrived on, usually the audio thread, so the
		// listeners are invoked on the message thread instead of here
		auto* self = this;

		juce::MessageManager::callAsync([self, note]
		{
			std::lock_guard lock(getInstancesMutex());

			if (getInstances().find(self) == getInstances().end())
				return;

			self->evNote(note);
		});
	}

	bool MidiNotifier::toNote(const synthLib::SMidiEvent& _ev, Note& _note)
	{
		const auto status = static_cast<uint8_t>(_ev.a & 0xf0);

		if (status != synthLib::M_NOTEON && status != synthLib::M_NOTEOFF)
			return false;

		_note.note = _ev.b & 0x7f;
		_note.velocity = _ev.c & 0x7f;
		_note.channel = static_cast<uint8_t>((_ev.a & 0x0f) + 1);

		// a note on with velocity zero is how a lot of gear says note off, and a skin drawing a
		// keyboard has to treat it as one or keys stay stuck down
		_note.on = status == synthLib::M_NOTEON && _note.velocity > 0;

		if (!_note.on)
			_note.velocity = 0;

		return true;
	}

	MidiNotifier::ListenerId MidiNotifier::addNoteListener(const baseLib::Event<Note>::Callback& _callback)
	{
		const auto id = evNote.addListener(_callback);
		m_noteListenerCount.fetch_add(1, std::memory_order_relaxed);
		return id;
	}

	void MidiNotifier::removeNoteListener(const ListenerId _id)
	{
		if (!evNote.getListener(_id))
			return;

		evNote.removeListener(_id);
		m_noteListenerCount.fetch_sub(1, std::memory_order_relaxed);
	}
}
