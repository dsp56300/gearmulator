#pragma once

struct lua_State;

namespace juceRmlUi
{
	// Registers the "Strokes" Lua type, the <strokes> element. Call once, after Rml::Lua::Initialise.
	void registerStrokesLua(lua_State* _L);
}
