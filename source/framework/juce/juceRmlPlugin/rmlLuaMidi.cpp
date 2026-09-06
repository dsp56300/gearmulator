#include "rmlLuaMidi.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/processor.h"

#include "RmlUi/Core/Log.h"
#include "RmlUi/Lua/IncludeLua.h"

namespace rmlPlugin
{
	using Note = pluginLib::MidiNotifier::Note;

	static const char* const MIDI_LISTENERS_REGISTRY_KEY = "RmlPlugin.LuaMidiListeners";

	namespace
	{
		LuaMidiListeners& getMidiListeners(lua_State* _L)
		{
			lua_getfield(_L, LUA_REGISTRYINDEX, MIDI_LISTENERS_REGISTRY_KEY);
			auto* l = static_cast<LuaMidiListeners*>(lua_touserdata(_L, -1));
			lua_pop(_L, 1);
			return *l;
		}

		int addListener(lua_State* _L, const bool _wantNoteOn)
		{
			luaL_checktype(_L, 1, LUA_TFUNCTION);
			lua_pushvalue(_L, 1);
			const int funcRef = luaL_ref(_L, LUA_REGISTRYINDEX);

			lua_pushinteger(_L, getMidiListeners(_L).add(_wantNoteOn, funcRef));
			return 1;
		}

		// midi.onNoteOn(func) -> id, callback receives (note, velocity, channel)
		int luaMidiOnNoteOn(lua_State* _L)
		{
			return addListener(_L, true);
		}

		// midi.onNoteOff(func) -> id, callback receives (note, channel)
		int luaMidiOnNoteOff(lua_State* _L)
		{
			return addListener(_L, false);
		}

		// midi.removeListener(id)
		int luaMidiRemoveListener(lua_State* _L)
		{
			getMidiListeners(_L).remove(static_cast<int>(luaL_checkinteger(_L, 1)));
			return 0;
		}
	}

	LuaMidiListeners::LuaMidiListeners(lua_State* _luaState, pluginLib::MidiNotifier& _notifier)
	: m_luaState(_luaState)
	, m_notifier(_notifier)
	, m_listenerId(pluginLib::MidiNotifier::ListenerId())
	{
		m_listenerId = m_notifier.addNoteListener([this](const Note& _note)
		{
			onNote(_note);
		});
	}

	LuaMidiListeners::~LuaMidiListeners()
	{
		m_notifier.removeNoteListener(m_listenerId);
		removeAll();
	}

	int LuaMidiListeners::add(const bool _wantNoteOn, const int _luaFuncRef)
	{
		const int id = m_nextId++;

		Entry e;
		e.wantNoteOn = _wantNoteOn;
		e.luaFuncRef = _luaFuncRef;

		m_entries.emplace(id, e);
		return id;
	}

	bool LuaMidiListeners::remove(const int _id)
	{
		const auto it = m_entries.find(_id);

		if (it == m_entries.end())
			return false;

		luaL_unref(m_luaState, LUA_REGISTRYINDEX, it->second.luaFuncRef);
		m_entries.erase(it);
		return true;
	}

	void LuaMidiListeners::removeAll()
	{
		for (const auto& [id, e] : m_entries)
			luaL_unref(m_luaState, LUA_REGISTRYINDEX, e.luaFuncRef);
		m_entries.clear();
	}

	void LuaMidiListeners::onNote(const Note& _note) const
	{
		for (const auto& [id, e] : m_entries)
		{
			if (e.wantNoteOn != _note.on)
				continue;

			lua_rawgeti(m_luaState, LUA_REGISTRYINDEX, e.luaFuncRef);

			lua_pushinteger(m_luaState, _note.note);

			int argCount = 2;

			// note on reports the velocity, note off has none to report
			if (_note.on)
			{
				lua_pushinteger(m_luaState, _note.velocity);
				++argCount;
			}

			lua_pushinteger(m_luaState, _note.channel);

			if (lua_pcall(m_luaState, argCount, 0, 0) != LUA_OK)
			{
				if (const char* err = lua_tostring(m_luaState, -1))
					Rml::Log::Message(Rml::Log::LT_WARNING, "Lua midi callback error: %s", err);
				lua_pop(m_luaState, 1);
			}
		}
	}

	void registerLuaMidi(lua_State* _L, pluginLib::Controller& _controller)
	{
		lua_pushlightuserdata(_L, new LuaMidiListeners(_L, _controller.getProcessor().getMidiNotifier()));
		lua_setfield(_L, LUA_REGISTRYINDEX, MIDI_LISTENERS_REGISTRY_KEY);

		static const luaL_Reg funcs[] = {
			{"onNoteOn", luaMidiOnNoteOn},
			{"onNoteOff", luaMidiOnNoteOff},
			{"removeListener", luaMidiRemoveListener},
			{nullptr, nullptr}
		};

		lua_newtable(_L);
		luaL_setfuncs(_L, funcs, 0);
		lua_setglobal(_L, "midi");
	}

	void unregisterLuaMidi(lua_State* _L)
	{
		lua_getfield(_L, LUA_REGISTRYINDEX, MIDI_LISTENERS_REGISTRY_KEY);
		auto* listeners = static_cast<LuaMidiListeners*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);

		delete listeners;

		lua_pushnil(_L);
		lua_setfield(_L, LUA_REGISTRYINDEX, MIDI_LISTENERS_REGISTRY_KEY);

		lua_pushnil(_L);
		lua_setglobal(_L, "midi");
	}
}
