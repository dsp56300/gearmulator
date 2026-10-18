#pragma once

#include <map>
#include <memory>
#include <string>
#include <cstdint>

#include "baseLib/event.h"
#include "jucePluginLib/parameterlistener.h"

typedef struct lua_State lua_State;

namespace juceRmlUi
{
	struct RmlInterfaces;
}

namespace pluginLib
{
	class Controller;
	class Parameter;
}

namespace rmlPlugin
{
	class LuaParameterListeners
	{
	public:
		LuaParameterListeners(lua_State* _L, pluginLib::Controller& _controller, juceRmlUi::RmlInterfaces& _rmlInterfaces);
		~LuaParameterListeners();

		LuaParameterListeners(const LuaParameterListeners&) = delete;
		LuaParameterListeners& operator=(const LuaParameterListeners&) = delete;

		int addParameterListener(const std::string& _name, uint8_t _part, bool _isCurrentPart, int _luaFuncRef);
		int addPartChangedListener(int _luaFuncRef);
		int addRegionLockListener(int _luaFuncRef);
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

		struct RegionLockEntry
		{
			baseLib::EventListener<uint8_t, std::string, bool> listener;
			int luaFuncRef = -1;
		};

		void rebindCurrentPartListeners(uint8_t _newPart);

		void postParamCallback(int _id, const pluginLib::Parameter& _param) const;
		void invokeParamCallback(int _id, int _value, const std::string& _text) const;
		void invokePartChangedCallback(int _id, uint8_t _part) const;
		void invokeRegionLockCallback(int _id, const std::string& _region, bool _locked, uint8_t _part) const;

		lua_State* m_luaState;
		pluginLib::Controller& m_controller;
		juceRmlUi::RmlInterfaces& m_rmlInterfaces;
		uint8_t m_lastPart = 0;
		std::map<int, ParamListenerEntry> m_paramListeners;
		std::map<int, PartChangedEntry> m_partChangedListeners;
		std::map<int, RegionLockEntry> m_regionLockListeners;
		baseLib::EventListener<uint8_t> m_currentPartListener;
		int m_nextId = 1;

		// The callbacks are posted to the message loop. One that runs after this object is gone - the skin
		// was switched or the editor closed, and the Lua state is closed right after - must not run at all
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};

	void registerLuaParameters(lua_State* _L, pluginLib::Controller& _controller, juceRmlUi::RmlInterfaces& _rmlInterfaces);
	void unregisterLuaParameters(lua_State* _L);
}
