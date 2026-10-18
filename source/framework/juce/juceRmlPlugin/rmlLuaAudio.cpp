#include "rmlLuaAudio.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/processor.h"

#include "RmlUi/Lua/IncludeLua.h"

namespace rmlPlugin
{
	static const char* const OUTPUT_LEVELS_REGISTRY_KEY = "RmlPlugin.OutputLevels";

	static pluginLib::AudioLevels& getOutputLevels(lua_State* _L)
	{
		lua_getfield(_L, LUA_REGISTRYINDEX, OUTPUT_LEVELS_REGISTRY_KEY);
		auto* levels = static_cast<pluginLib::AudioLevels*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);
		return *levels;
	}

	// audio.getOutputCount() -> number of output channels
	static int luaAudioGetOutputCount(lua_State* _L)
	{
		lua_pushinteger(_L, getOutputLevels(_L).getChannelCount());
		return 1;
	}

	// audio.getOutputLevel(channel) -> peak, rms. Channels count from 0, linear, 1 is full scale. Nil for a channel
	// that does not exist
	static int luaAudioGetOutputLevel(lua_State* _L)
	{
		const auto channel = luaL_checkinteger(_L, 1);

		float peak = 0.0f, rms = 0.0f;
		if (channel < 0 || !getOutputLevels(_L).getLevel(static_cast<uint32_t>(channel), peak, rms))
		{
			lua_pushnil(_L);
			return 1;
		}

		lua_pushnumber(_L, peak);
		lua_pushnumber(_L, rms);
		return 2;
	}

	void registerLuaAudio(lua_State* _L, pluginLib::Controller& _controller)
	{
		lua_pushlightuserdata(_L, &_controller.getProcessor().getOutputLevels());
		lua_setfield(_L, LUA_REGISTRYINDEX, OUTPUT_LEVELS_REGISTRY_KEY);

		static const luaL_Reg funcs[] = {
			{"getOutputCount", luaAudioGetOutputCount},
			{"getOutputLevel", luaAudioGetOutputLevel},
			{nullptr, nullptr}
		};

		lua_newtable(_L);
		luaL_setfuncs(_L, funcs, 0);
		lua_setglobal(_L, "audio");
	}

	void unregisterLuaAudio(lua_State* _L)
	{
		lua_getfield(_L, LUA_REGISTRYINDEX, OUTPUT_LEVELS_REGISTRY_KEY);
		auto* levels = static_cast<pluginLib::AudioLevels*>(lua_touserdata(_L, -1));
		lua_pop(_L, 1);

		// nobody reads them any more, the audio thread stops measuring
		if (levels)
			levels->stop();

		lua_pushnil(_L);
		lua_setfield(_L, LUA_REGISTRYINDEX, OUTPUT_LEVELS_REGISTRY_KEY);

		lua_pushnil(_L);
		lua_setglobal(_L, "audio");
	}
}
