#pragma once

struct lua_State;

namespace pluginLib
{
	class Controller;
}

namespace rmlPlugin
{
	// the "leds" table: front panel LEDs of the device, see pluginLib::Controller::getLedBrightness
	void registerLuaLeds(lua_State* _L, pluginLib::Controller& _controller);
	void unregisterLuaLeds(lua_State* _L);
}
