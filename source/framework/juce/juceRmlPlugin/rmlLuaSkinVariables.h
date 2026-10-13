#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "baseLib/event.h"

#include "jucePluginLib/skinVariables.h"

struct lua_State;

namespace juceRmlUi
{
	struct RmlInterfaces;
}

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
		LuaSkinVariableListeners(lua_State* _luaState, pluginLib::SkinVariables& _variables, juceRmlUi::RmlInterfaces& _rmlInterfaces);
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

		// a change as the callbacks report it, with the values read when it happened
		struct Change
		{
			std::string name;
			pluginLib::SkinVariables::Scope scope = pluginLib::SkinVariables::Scope::Instance;
			std::optional<pluginLib::SkinVariables::Value> valueInScope;	// for an entry that asked for a scope
			std::optional<pluginLib::SkinVariables::Value> value;			// for one that did not, instance before global
		};

		void onChanged(const std::string& _name, pluginLib::SkinVariables::Scope _scope) const;
		std::vector<int> findEntries(const Change& _change) const;
		void invoke(int _id, const Change& _change) const;

		lua_State* m_luaState;
		pluginLib::SkinVariables& m_variables;
		juceRmlUi::RmlInterfaces& m_rmlInterfaces;
		baseLib::EventListener<std::string, pluginLib::SkinVariables::Scope> m_listener;
		std::map<int, Entry> m_entries;
		int m_nextId = 1;

		// the callbacks are posted to the message loop, and one that runs after this object is gone, with the
		// Lua state closed right after it, must not run at all
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};

	void registerLuaSkinVariables(lua_State* _L, pluginLib::Controller& _controller, juceRmlUi::RmlInterfaces& _rmlInterfaces);
	void unregisterLuaSkinVariables(lua_State* _L);
}
