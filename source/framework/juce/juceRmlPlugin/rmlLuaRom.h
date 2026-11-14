#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "baseLib/event.h"

struct lua_State;

namespace juceRmlUi
{
	struct RmlInterfaces;
}

namespace pluginLib
{
	class Controller;
	class Processor;
}

namespace rmlPlugin
{
	// the "rom" table: the ROMs a product can switch between, see pluginLib::Processor::getFirmwareNames, and the Lua
	// callbacks registered through rom.onChange
	class LuaRomListeners
	{
	public:
		LuaRomListeners(lua_State* _luaState, pluginLib::Processor& _processor, juceRmlUi::RmlInterfaces& _rmlInterfaces);
		~LuaRomListeners();

		LuaRomListeners(const LuaRomListeners&) = delete;
		LuaRomListeners& operator=(const LuaRomListeners&) = delete;

		int add(int _luaFuncRef);
		bool remove(int _id);
		void removeAll();

		pluginLib::Processor& getProcessor() const { return m_processor; }

	private:
		std::vector<int> getIds() const;
		void invoke(int _id, uint32_t _index) const;

		lua_State* m_luaState;
		pluginLib::Processor& m_processor;
		juceRmlUi::RmlInterfaces& m_rmlInterfaces;
		baseLib::EventListener<uint32_t> m_listener;
		std::map<int, int> m_luaFuncRefs;	// by id
		int m_nextId = 1;

		// the callbacks are posted to the message loop, and one that runs after this object is gone, with the
		// Lua state closed right after it, must not run at all
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};

	void registerLuaRom(lua_State* _L, pluginLib::Controller& _controller, juceRmlUi::RmlInterfaces& _rmlInterfaces);
	void unregisterLuaRom(lua_State* _L);
}
