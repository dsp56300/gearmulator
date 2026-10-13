#include "rmlLuaSkinVariables.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/processor.h"

#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Log.h"
#include "RmlUi/Lua/IncludeLua.h"

#include "juce_events/juce_events.h"

namespace rmlPlugin
{
	using Scope = pluginLib::SkinVariables::Scope;
	using Value = pluginLib::SkinVariables::Value;

	static const char* const VARIABLES_REGISTRY_KEY = "RmlPlugin.SkinVariables";
	static const char* const VAR_LISTENERS_REGISTRY_KEY = "RmlPlugin.LuaSkinVariableListeners";

	namespace
	{
		pluginLib::SkinVariables& getVariables(lua_State* _L)
		{
			lua_getfield(_L, LUA_REGISTRYINDEX, VARIABLES_REGISTRY_KEY);
			auto* v = static_cast<pluginLib::SkinVariables*>(lua_touserdata(_L, -1));
			lua_pop(_L, 1);
			return *v;
		}

		LuaSkinVariableListeners& getVarListeners(lua_State* _L)
		{
			lua_getfield(_L, LUA_REGISTRYINDEX, VAR_LISTENERS_REGISTRY_KEY);
			auto* l = static_cast<LuaSkinVariableListeners*>(lua_touserdata(_L, -1));
			lua_pop(_L, 1);
			return *l;
		}

		// "instance" or "global". Absent means: the caller did not care, which the read and the
		// change callback treat differently from an explicit scope.
		bool readScope(lua_State* _L, const int _argIndex, Scope& _scope)
		{
			if (lua_gettop(_L) < _argIndex || lua_isnoneornil(_L, _argIndex))
				return false;

			const char* s = luaL_checkstring(_L, _argIndex);

			if (strcmp(s, "global") == 0)
			{
				_scope = Scope::Global;
				return true;
			}
			if (strcmp(s, "instance") == 0)
			{
				_scope = Scope::Instance;
				return true;
			}

			luaL_error(_L, "scope must be 'instance' or 'global', got '%s'", s);
			return false;
		}

		void pushValue(lua_State* _L, const Value& _value)
		{
			if (const auto* i = std::get_if<int64_t>(&_value))
				lua_pushinteger(_L, static_cast<lua_Integer>(*i));
			else
				lua_pushstring(_L, std::get<std::string>(_value).c_str());
		}

		// skinvars.set(name, value [, scope])
		int luaVarsSet(lua_State* _L)
		{
			const char* name = luaL_checkstring(_L, 1);

			Value value;

			if (lua_type(_L, 2) == LUA_TSTRING)
			{
				value = std::string(lua_tostring(_L, 2));
			}
			else if (lua_isnumber(_L, 2))
			{
				value = static_cast<int64_t>(llround(lua_tonumber(_L, 2)));
			}
			else if (lua_isboolean(_L, 2))
			{
				value = static_cast<int64_t>(lua_toboolean(_L, 2) ? 1 : 0);
			}
			else
			{
				return luaL_error(_L, "skinvars.set: value must be a number or a string");
			}

			auto scope = Scope::Instance;
			readScope(_L, 3, scope);

			getVariables(_L).set(name, value, scope);
			return 0;
		}

		// skinvars.get(name [, scope]) -> value or nil
		int luaVarsGet(lua_State* _L)
		{
			const char* name = luaL_checkstring(_L, 1);

			auto& vars = getVariables(_L);

			Scope scope;
			const Value* v = readScope(_L, 2, scope) ? vars.get(name, scope) : vars.get(name);

			if (!v)
			{
				lua_pushnil(_L);
				return 1;
			}

			pushValue(_L, *v);
			return 1;
		}

		// skinvars.remove(name [, scope]) -> bool
		int luaVarsRemove(lua_State* _L)
		{
			const char* name = luaL_checkstring(_L, 1);

			auto scope = Scope::Instance;
			readScope(_L, 2, scope);

			lua_pushboolean(_L, getVariables(_L).remove(name, scope) ? 1 : 0);
			return 1;
		}

		// skinvars.onChange(name, func [, scope]) -> id
		int luaVarsOnChange(lua_State* _L)
		{
			const char* name = luaL_checkstring(_L, 1);
			luaL_checktype(_L, 2, LUA_TFUNCTION);

			auto scope = Scope::Instance;
			const bool hasScope = readScope(_L, 3, scope);

			lua_pushvalue(_L, 2);
			const int funcRef = luaL_ref(_L, LUA_REGISTRYINDEX);

			lua_pushinteger(_L, getVarListeners(_L).add(name, hasScope, scope, funcRef));
			return 1;
		}

		// skinvars.removeListener(id)
		int luaVarsRemoveListener(lua_State* _L)
		{
			getVarListeners(_L).remove(static_cast<int>(luaL_checkinteger(_L, 1)));
			return 0;
		}
	}

	LuaSkinVariableListeners::LuaSkinVariableListeners(lua_State* _luaState, pluginLib::SkinVariables& _variables, juceRmlUi::RmlInterfaces& _rmlInterfaces)
	: m_luaState(_luaState)
	, m_variables(_variables)
	, m_rmlInterfaces(_rmlInterfaces)
	{
		m_listener.set(m_variables.evChanged, [this](const std::string& _name, const Scope _scope)
		{
			onChanged(_name, _scope);
		});
	}

	LuaSkinVariableListeners::~LuaSkinVariableListeners()
	{
		removeAll();
	}

	int LuaSkinVariableListeners::add(std::string _name, const bool _hasScope, const Scope _scope, const int _luaFuncRef)
	{
		const int id = m_nextId++;

		Entry e;
		e.name = std::move(_name);
		e.hasScope = _hasScope;
		e.scope = _scope;
		e.luaFuncRef = _luaFuncRef;

		m_entries.emplace(id, std::move(e));
		return id;
	}

	bool LuaSkinVariableListeners::remove(const int _id)
	{
		const auto it = m_entries.find(_id);

		if (it == m_entries.end())
			return false;

		luaL_unref(m_luaState, LUA_REGISTRYINDEX, it->second.luaFuncRef);
		m_entries.erase(it);
		return true;
	}

	void LuaSkinVariableListeners::removeAll()
	{
		for (const auto& [id, e] : m_entries)
			luaL_unref(m_luaState, LUA_REGISTRYINDEX, e.luaFuncRef);
		m_entries.clear();
	}

	void LuaSkinVariableListeners::onChanged(const std::string& _name, const Scope _scope) const
	{
		Change change;
		change.name = _name;
		change.scope = _scope;

		// the values are read here rather than in the callback, so that each callback reports the change it
		// stands for. An entry without a scope sees what a plain get() would answer, instance winning over global
		if (const auto* v = m_variables.get(_name, _scope))
			change.valueInScope = *v;
		if (const auto* v = m_variables.get(_name))
			change.value = *v;

		// On the message thread the entries are picked now, so that one registered after the change does not
		// hear of it. A state restore changes the variables on whichever thread the host restores on, and the
		// entries belong to the message thread, so they are picked once the callback runs there.
		std::optional<std::vector<int>> ids;

		if (juce::MessageManager::existsAndIsCurrentThread())
			ids = findEntries(change);

		juce::MessageManager::callAsync([this, alive = std::weak_ptr(m_alive), change = std::move(change), ids = std::move(ids)]
		{
			if (alive.expired())
				return;

			for (const auto id : ids ? *ids : findEntries(change))
				invoke(id, change);
		});
	}

	std::vector<int> LuaSkinVariableListeners::findEntries(const Change& _change) const
	{
		std::vector<int> ids;

		for (const auto& [id, e] : m_entries)
		{
			if (e.name != _change.name)
				continue;

			// no scope given at registration means "whichever scope it changed in"
			if (e.hasScope && e.scope != _change.scope)
				continue;

			ids.push_back(id);
		}

		return ids;
	}

	void LuaSkinVariableListeners::invoke(const int _id, const Change& _change) const
	{
		// removed while the call was queued, or by an earlier callback for the same change
		const auto it = m_entries.find(_id);
		if (it == m_entries.end())
			return;

		const auto& value = it->second.hasScope ? _change.valueInScope : _change.value;

		// the callback is free to change the DOM, which the render thread walks
		juceRmlUi::RmlInterfaces::ScopedAccess access(m_rmlInterfaces);

		auto* L = m_luaState;

		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.luaFuncRef);

		if (value)
			pushValue(L, *value);
		else
			lua_pushnil(L);

		lua_pushstring(L, _change.scope == Scope::Global ? "global" : "instance");

		if (lua_pcall(L, 2, 0, 0) != LUA_OK)
		{
			if (const char* err = lua_tostring(L, -1))
				Rml::Log::Message(Rml::Log::LT_WARNING, "Lua skinvars callback error: %s", err);
			lua_pop(L, 1);
		}
	}

	void registerLuaSkinVariables(lua_State* _L, pluginLib::Controller& _controller, juceRmlUi::RmlInterfaces& _rmlInterfaces)
	{
		// a registration that was never undone would keep its listeners, and the callbacks they post, past the
		// end of the Lua state
		unregisterLuaSkinVariables(_L);

		auto& variables = _controller.getProcessor().getSkinVariables();

		lua_pushlightuserdata(_L, &variables);
		lua_setfield(_L, LUA_REGISTRYINDEX, VARIABLES_REGISTRY_KEY);

		lua_pushlightuserdata(_L, new LuaSkinVariableListeners(_L, variables, _rmlInterfaces));
		lua_setfield(_L, LUA_REGISTRYINDEX, VAR_LISTENERS_REGISTRY_KEY);

		static const luaL_Reg funcs[] = {
			{"set", luaVarsSet},
			{"get", luaVarsGet},
			{"remove", luaVarsRemove},
			{"onChange", luaVarsOnChange},
			{"removeListener", luaVarsRemoveListener},
			{nullptr, nullptr}
		};

		lua_newtable(_L);
		luaL_setfuncs(_L, funcs, 0);
		lua_setglobal(_L, "skinvars");
	}

	void unregisterLuaSkinVariables(lua_State* _L)
	{
		lua_getfield(_L, LUA_REGISTRYINDEX, VAR_LISTENERS_REGISTRY_KEY);
		auto* listeners = static_cast<LuaSkinVariableListeners*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);

		delete listeners;

		lua_pushnil(_L);
		lua_setfield(_L, LUA_REGISTRYINDEX, VAR_LISTENERS_REGISTRY_KEY);

		lua_pushnil(_L);
		lua_setfield(_L, LUA_REGISTRYINDEX, VARIABLES_REGISTRY_KEY);

		lua_pushnil(_L);
		lua_setglobal(_L, "skinvars");
	}
}
