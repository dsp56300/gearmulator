#pragma once

struct lua_State;

namespace pluginLib
{
	class Controller;
}

namespace rmlPlugin
{
	// the "audio" table: levels of the plugin's output channels for meters, see pluginLib::AudioLevels
	void registerLuaAudio(lua_State* _L, pluginLib::Controller& _controller);
	void unregisterLuaAudio(lua_State* _L);
}
