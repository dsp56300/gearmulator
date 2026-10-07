#include "rmlLuaStrokes.h"

#include "rmlElemStrokes.h"

#include "RmlUi/Lua/LuaType.h"
#include "RmlUi/Lua/Utilities.h"	// AddTypeToElementAsTable

#include "Lua/Element.h"			// ExtraInit<Element>, LuaType<Element> (from RmlUi/Source)

namespace Rml
{
	namespace Lua
	{
		// The <strokes> element, cast via Element.As.Strokes(el). Method arguments start at stack index 1, the LuaType
		// thunk removes 'self' and passes it as the C++ pointer
		using Strokes = juceRmlUi::ElemStrokes;

		namespace
		{
			// the value at the top of the stack as {x, y}
			bool toPoint(lua_State* L, Rml::Vector2f& _point)
			{
				if (!lua_istable(L, -1))
					return false;

				int isX = 0, isY = 0;
				lua_rawgeti(L, -1, 1);
				lua_rawgeti(L, -2, 2);
				_point.x = static_cast<float>(lua_tonumberx(L, -2, &isX));
				_point.y = static_cast<float>(lua_tonumberx(L, -1, &isY));
				lua_pop(L, 2);
				return isX && isY;
			}
		}

		// strokes:setStrokes({ { {x, y}, {x, y}, ... }, ... }): one table of points per line, in dp relative to the
		// element's top left. An empty table removes all lines
		int StrokessetStrokes(lua_State* L, Strokes* strokes)
		{
			RMLUI_CHECK_OBJ(strokes);

			// not luaL_checktype: called as a method, its message would blame self
			if (!lua_istable(L, 1))
				return luaL_error(L, "setStrokes: expects a table of lines");

			std::vector<Strokes::Points> lines(lua_rawlen(L, 1));

			for (size_t l = 0; l < lines.size(); ++l)
			{
				if (lua_rawgeti(L, 1, static_cast<lua_Integer>(l + 1)) != LUA_TTABLE)
					return luaL_error(L, "setStrokes: line %d is not a table of points", static_cast<int>(l + 1));

				auto& points = lines[l];
				points.resize(lua_rawlen(L, -1));

				for (size_t p = 0; p < points.size(); ++p)
				{
					lua_rawgeti(L, -1, static_cast<lua_Integer>(p + 1));
					if (!toPoint(L, points[p]))
						return luaL_error(L, "setStrokes: point %d of line %d is not {x, y}", static_cast<int>(p + 1),
							static_cast<int>(l + 1));
					lua_pop(L, 1);
				}

				lua_pop(L, 1);
			}

			strokes->setStrokes(std::move(lines));
			return 0;
		}

		RegType<Strokes> StrokesMethods[] = {
			RMLUI_LUAMETHOD(Strokes, setStrokes),
			{ nullptr, nullptr },
		};

		luaL_Reg StrokesGetters[] = { { nullptr, nullptr } };
		luaL_Reg StrokesSetters[] = { { nullptr, nullptr } };

		template <>
		void ExtraInit<Strokes>(lua_State* L, const int metatable_index)
		{
			// inherit from Element
			ExtraInit<Element>(L, metatable_index);
			LuaType<Element>::_regfunctions(L, metatable_index, metatable_index - 1);
			AddTypeToElementAsTable<Strokes>(L);
		}
		RMLUI_LUATYPE_DEFINE(Strokes)
	}
}

namespace juceRmlUi
{
	void registerStrokesLua(lua_State* _L)
	{
		if (_L)
			Rml::Lua::LuaType<ElemStrokes>::Register(_L);
	}
}
