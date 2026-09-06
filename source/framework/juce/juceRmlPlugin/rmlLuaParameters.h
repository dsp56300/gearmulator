#pragma once

#include <map>
#include <string>
#include <cstdint>

#include "baseLib/event.h"
#include "jucePluginLib/parameterlistener.h"

typedef struct lua_State lua_State;

namespace pluginLib
{
	class Controller;
}

namespace rmlPlugin
{
	class LuaParameterListeners
	{
	public:
		LuaParameterListeners(lua_State* _L, pluginLib::Controller& _controller);
		~LuaParameterListeners();

		LuaParameterListeners(const LuaParameterListeners&) = delete;
		LuaParameterListeners& operator=(const LuaParameterListeners&) = delete;

		int addParameterListener(const std::string& _name, uint8_t _part, bool _isCurrentPart, int _luaFuncRef);
		int addPartChangedListener(int _luaFuncRef);
		void removeListener(int _id);
		void removeAllListeners();

	private:
		struct ParamListenerEntry
		{
			pluginLib::ParameterListener listener;
			int luaFuncRef = -1;
			std::string paramName;
			bool isCurrentPart = false;
		};

		struct PartChangedEntry
		{
			baseLib::EventListener<uint8_t> listener;
			int luaFuncRef = -1;
		};

		void rebindCurrentPartListeners(uint8_t _newPart);

		lua_State* m_luaState;
		pluginLib::Controller& m_controller;
		uint8_t m_lastPart = 0;
		std::map<int, ParamListenerEntry> m_paramListeners;
		std::map<int, PartChangedEntry> m_partChangedListeners;
		baseLib::EventListener<uint8_t> m_currentPartListener;
		int m_nextId = 1;
	};

	void registerLuaParameters(lua_State* _L, pluginLib::Controller& _controller);
	void unregisterLuaParameters(lua_State* _L);
}
