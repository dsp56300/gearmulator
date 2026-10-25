#include "rmlLuaLeds.h"

#include "jucePluginLib/controller.h"

#include "RmlUi/Lua/IncludeLua.h"

namespace rmlPlugin
{
	static const char* const LEDS_CONTROLLER_REGISTRY_KEY = "RmlPlugin.LedsController";

	// leds.get(name) -> brightness, 0 is off, 1 fully lit. Nil for an LED the device does not have
	static int luaLedsGet(lua_State* _L)
	{
		const char* name = luaL_checkstring(_L, 1);

		lua_getfield(_L, LUA_REGISTRYINDEX, LEDS_CONTROLLER_REGISTRY_KEY);
		const auto* controller = static_cast<const pluginLib::Controller*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);

		float brightness = 0.0f;

		if (!controller || !controller->getLedBrightness(name, brightness))
			lua_pushnil(_L);
		else
			lua_pushnumber(_L, brightness);

		return 1;
	}

	void registerLuaLeds(lua_State* _L, pluginLib::Controller& _controller)
	{
		lua_pushlightuserdata(_L, &_controller);
		lua_setfield(_L, LUA_REGISTRYINDEX, LEDS_CONTROLLER_REGISTRY_KEY);

		static const luaL_Reg funcs[] = {
			{"get", luaLedsGet},
			{nullptr, nullptr}
		};

		lua_newtable(_L);
		luaL_setfuncs(_L, funcs, 0);
		lua_setglobal(_L, "leds");
	}

	void unregisterLuaLeds(lua_State* _L)
	{
		lua_pushnil(_L);
		lua_setfield(_L, LUA_REGISTRYINDEX, LEDS_CONTROLLER_REGISTRY_KEY);

		lua_pushnil(_L);
		lua_setglobal(_L, "leds");
	}
}
