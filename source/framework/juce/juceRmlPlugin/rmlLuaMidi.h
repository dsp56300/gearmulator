#pragma once

#include <map>

#include "baseLib/event.h"

#include "jucePluginLib/midiNotifier.h"

struct lua_State;

namespace pluginLib
{
	class Controller;
}

namespace rmlPlugin
{
	// Lua callbacks registered through midi.onNoteOn / midi.onNoteOff. The notifier already hands
	// notes over on the message thread, so these are invoked directly.
	class LuaMidiListeners
	{
	public:
		LuaMidiListeners(lua_State* _luaState, pluginLib::MidiNotifier& _notifier);
		~LuaMidiListeners();

		LuaMidiListeners(const LuaMidiListeners&) = delete;
		LuaMidiListeners& operator=(const LuaMidiListeners&) = delete;

		int add(bool _wantNoteOn, int _luaFuncRef);
		bool remove(int _id);
		void removeAll();

	private:
		struct Entry
		{
			bool wantNoteOn = true;
			int luaFuncRef = 0;
		};

		void onNote(const pluginLib::MidiNotifier::Note& _note) const;

		lua_State* m_luaState;
		pluginLib::MidiNotifier& m_notifier;
		pluginLib::MidiNotifier::ListenerId m_listenerId;
		std::map<int, Entry> m_entries;
		int m_nextId = 1;
	};

	void registerLuaMidi(lua_State* _L, pluginLib::Controller& _controller);
	void unregisterLuaMidi(lua_State* _L);
}
