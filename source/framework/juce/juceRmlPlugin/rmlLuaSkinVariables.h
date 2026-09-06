#pragma once

#include <map>
#include <string>

#include "baseLib/event.h"

#include "jucePluginLib/skinVariables.h"

struct lua_State;

namespace pluginLib
{
	class Controller;
}

namespace rmlPlugin
{
	// Lua callbacks registered through skinvars.onChange. One event listener feeds all of them; the
	// entries filter by variable name and, if the skin asked for it, by scope.
	class LuaSkinVariableListeners
	{
	public:
		LuaSkinVariableListeners(lua_State* _luaState, pluginLib::SkinVariables& _variables);
		~LuaSkinVariableListeners();

		LuaSkinVariableListeners(const LuaSkinVariableListeners&) = delete;
		LuaSkinVariableListeners& operator=(const LuaSkinVariableListeners&) = delete;

		int add(std::string _name, bool _hasScope, pluginLib::SkinVariables::Scope _scope, int _luaFuncRef);
		bool remove(int _id);
		void removeAll();

	private:
		struct Entry
		{
			std::string name;
			bool hasScope = false;
			pluginLib::SkinVariables::Scope scope = pluginLib::SkinVariables::Scope::Instance;
			int luaFuncRef = 0;
		};

		void onChanged(const std::string& _name, pluginLib::SkinVariables::Scope _scope) const;

		lua_State* m_luaState;
		pluginLib::SkinVariables& m_variables;
		baseLib::EventListener<std::string, pluginLib::SkinVariables::Scope> m_listener;
		std::map<int, Entry> m_entries;
		int m_nextId = 1;
	};

	void registerLuaSkinVariables(lua_State* _L, pluginLib::Controller& _controller);
	void unregisterLuaSkinVariables(lua_State* _L);
}
