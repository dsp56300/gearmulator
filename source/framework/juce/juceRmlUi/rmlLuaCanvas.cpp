#include "rmlLuaCanvas.h"

#include "rmlElemCanvas.h"

#include <cstdio>
#include <memory>

#include "RmlUi/Core/Log.h"
#include "RmlUi/Lua/LuaType.h"
#include "RmlUi/Lua/Utilities.h"	// AddTypeToElementAsTable

#include "Lua/Element.h"			// ExtraInit<Element>, LuaType<Element> (from RmlUi/Source)

namespace Rml
{
	namespace Lua
	{
		// Lua-visible type names are derived from these aliases:
		//   Canvas        -> the <canvas> element, cast via Element.As.Canvas(el)
		//   CanvasContext -> the 2D drawing context passed to the paint function
		//
		// Method functions receive their Lua arguments starting at stack index 1
		// (the LuaType thunk removes 'self' and passes it as the C++ pointer).
		// Property getters/setters are not thunked: self is at index 1, the
		// assigned value at index 2.
		using Canvas = juceRmlUi::ElemCanvas;
		using CanvasContext = juceRmlUi::Context2D;

		namespace
		{
			juce::Colour parseCssColour(const juce::String& _in, const juce::Colour _fallback)
			{
				const auto s = _in.trim();

				if (s.startsWithChar('#'))
				{
					const auto hex = s.substring(1);
					const auto v = static_cast<uint32_t>(hex.getHexValue64());

					switch (hex.length())
					{
					case 3:	// #rgb
						{
							const auto r = static_cast<uint8_t>(((v >> 8) & 0xf) * 0x11);
							const auto g = static_cast<uint8_t>(((v >> 4) & 0xf) * 0x11);
							const auto b = static_cast<uint8_t>((v & 0xf) * 0x11);
							return juce::Colour(r, g, b);
						}
					case 6:	// #rrggbb
						return juce::Colour(static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v), static_cast<uint8_t>(255));
					case 8:	// #rrggbbaa
						return juce::Colour(static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v));
					default:
						return _fallback;
					}
				}

				if (s.startsWithIgnoreCase("rgb"))
				{
					const auto open = s.indexOfChar('(');
					const auto close = s.lastIndexOfChar(')');
					if (open < 0 || close < open)
						return _fallback;

					juce::StringArray parts;
					parts.addTokens(s.substring(open + 1, close), ",", "");
					parts.trim();

					if (parts.size() < 3)
						return _fallback;

					const auto r = static_cast<uint8_t>(juce::jlimit(0, 255, parts[0].getIntValue()));
					const auto g = static_cast<uint8_t>(juce::jlimit(0, 255, parts[1].getIntValue()));
					const auto b = static_cast<uint8_t>(juce::jlimit(0, 255, parts[2].getIntValue()));
					const auto a = parts.size() >= 4
						? static_cast<uint8_t>(juce::jlimit(0, 255, juce::roundToInt(parts[3].getFloatValue() * 255.0f)))
						: static_cast<uint8_t>(255);
					return juce::Colour(r, g, b, a);
				}

				return _fallback;
			}

			juce::String colourToCss(const juce::Colour _c)
			{
				const auto hex2 = [](const uint8_t _v) { return juce::String::toHexString(&_v, 1, 0).paddedLeft('0', 2); };
				auto out = "#" + hex2(_c.getRed()) + hex2(_c.getGreen()) + hex2(_c.getBlue());
				if (_c.getAlpha() != 255)
					out += hex2(_c.getAlpha());
				return out;
			}

			juce::Graphics* gfx(const CanvasContext* _c) { return _c ? _c->graphics() : nullptr; }

			// HTML5 measures angles from the +x axis (3 o'clock), juce's
			// addCentredArc from 12 o'clock; both go clockwise in screen space.
			void addArc(CanvasContext* _c, const float _x, const float _y, const float _rx, const float _ry, const float _rot, const float _a0, const float _a1, const bool _ccw)
			{
				const auto quarter = juce::MathConstants<float>::halfPi;
				auto from = _a0 + quarter;
				auto to = _a1 + quarter;

				if (_ccw)
					std::swap(from, to);
				while (to < from)
					to += juce::MathConstants<float>::twoPi;

				const bool startNew = !_c->pathStarted();
				_c->path().addCentredArc(_x, _y, _rx, _ry, _rot, from, to, startNew);
				_c->pathStarted() = true;
			}
		}

		// ---------------------------------------------------------------------
		// CanvasContext (HTML5 CanvasRenderingContext2D subset)
		// method args start at stack index 1
		// ---------------------------------------------------------------------

		int CanvasContextfillRect(lua_State* L, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			const auto w = static_cast<float>(luaL_checknumber(L, 3));
			const auto h = static_cast<float>(luaL_checknumber(L, 4));
			g->setColour(c->fillColour);
			g->fillRect(juce::Rectangle<float>(x, y, w, h));
			return 0;
		}

		int CanvasContextstrokeRect(lua_State* L, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			const auto w = static_cast<float>(luaL_checknumber(L, 3));
			const auto h = static_cast<float>(luaL_checknumber(L, 4));
			g->setColour(c->strokeColour);
			g->drawRect(juce::Rectangle<float>(x, y, w, h), c->lineWidth);
			return 0;
		}

		int CanvasContextbeginPath(lua_State*, CanvasContext* c)
		{
			if (c)
			{
				c->path().clear();
				c->pathStarted() = false;
			}
			return 0;
		}

		int CanvasContextclosePath(lua_State*, CanvasContext* c)
		{
			if (c && c->pathStarted())
				c->path().closeSubPath();
			return 0;
		}

		int CanvasContextmoveTo(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			c->path().startNewSubPath(x, y);
			c->pathStarted() = true;
			return 0;
		}

		int CanvasContextlineTo(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			if (!c->pathStarted())
				c->path().startNewSubPath(x, y);	// HTML5: lineTo with no current point acts as moveTo
			else
				c->path().lineTo(x, y);
			c->pathStarted() = true;
			return 0;
		}

		int CanvasContextrect(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			const auto w = static_cast<float>(luaL_checknumber(L, 3));
			const auto h = static_cast<float>(luaL_checknumber(L, 4));
			c->path().addRectangle(juce::Rectangle<float>(x, y, w, h));
			c->pathStarted() = true;
			return 0;
		}

		int CanvasContextarc(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			const auto r = static_cast<float>(luaL_checknumber(L, 3));
			const auto a0 = static_cast<float>(luaL_checknumber(L, 4));
			const auto a1 = static_cast<float>(luaL_checknumber(L, 5));
			const bool ccw = lua_gettop(L) >= 6 && lua_toboolean(L, 6) != 0;
			addArc(c, x, y, r, r, 0.0f, a0, a1, ccw);
			return 0;
		}

		int CanvasContextellipse(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			const auto rx = static_cast<float>(luaL_checknumber(L, 3));
			const auto ry = static_cast<float>(luaL_checknumber(L, 4));
			const auto rot = static_cast<float>(luaL_checknumber(L, 5));
			const auto a0 = static_cast<float>(luaL_checknumber(L, 6));
			const auto a1 = static_cast<float>(luaL_checknumber(L, 7));
			const bool ccw = lua_gettop(L) >= 8 && lua_toboolean(L, 8) != 0;
			addArc(c, x, y, rx, ry, rot, a0, a1, ccw);
			return 0;
		}

		int CanvasContextfill(lua_State*, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			g->setColour(c->fillColour);
			g->fillPath(c->path());
			return 0;
		}

		int CanvasContextstroke(lua_State*, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			g->setColour(c->strokeColour);
			g->strokePath(c->path(), juce::PathStrokeType(c->lineWidth));
			return 0;
		}

		// getters / setters (dot-syntax properties): self at 1, value at 2

		int CanvasContextGetAttrfillStyle(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			lua_pushstring(L, colourToCss(c->fillColour).toRawUTF8());
			return 1;
		}
		int CanvasContextSetAttrfillStyle(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			c->fillColour = parseCssColour(luaL_checkstring(L, 2), c->fillColour);
			return 0;
		}

		int CanvasContextGetAttrstrokeStyle(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			lua_pushstring(L, colourToCss(c->strokeColour).toRawUTF8());
			return 1;
		}
		int CanvasContextSetAttrstrokeStyle(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			c->strokeColour = parseCssColour(luaL_checkstring(L, 2), c->strokeColour);
			return 0;
		}

		int CanvasContextGetAttrlineWidth(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			lua_pushnumber(L, c->lineWidth);
			return 1;
		}
		int CanvasContextSetAttrlineWidth(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			c->lineWidth = static_cast<float>(luaL_checknumber(L, 2));
			return 0;
		}

		RegType<CanvasContext> CanvasContextMethods[] = {
			RMLUI_LUAMETHOD(CanvasContext, fillRect),
			RMLUI_LUAMETHOD(CanvasContext, strokeRect),
			RMLUI_LUAMETHOD(CanvasContext, beginPath),
			RMLUI_LUAMETHOD(CanvasContext, closePath),
			RMLUI_LUAMETHOD(CanvasContext, moveTo),
			RMLUI_LUAMETHOD(CanvasContext, lineTo),
			RMLUI_LUAMETHOD(CanvasContext, rect),
			RMLUI_LUAMETHOD(CanvasContext, arc),
			RMLUI_LUAMETHOD(CanvasContext, ellipse),
			RMLUI_LUAMETHOD(CanvasContext, fill),
			RMLUI_LUAMETHOD(CanvasContext, stroke),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasContextGetters[] = {
			RMLUI_LUAGETTER(CanvasContext, fillStyle),
			RMLUI_LUAGETTER(CanvasContext, strokeStyle),
			RMLUI_LUAGETTER(CanvasContext, lineWidth),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasContextSetters[] = {
			RMLUI_LUASETTER(CanvasContext, fillStyle),
			RMLUI_LUASETTER(CanvasContext, strokeStyle),
			RMLUI_LUASETTER(CanvasContext, lineWidth),
			{ nullptr, nullptr },
		};

		template <>
		void ExtraInit<CanvasContext>(lua_State*, int)
		{
		}
		RMLUI_LUATYPE_DEFINE(CanvasContext)

		// ---------------------------------------------------------------------
		// Canvas element (extends Element); method args start at stack index 1
		// ---------------------------------------------------------------------

		namespace
		{
			// Owns the referenced Lua paint function and the drawing context for
			// one canvas; unrefs the function when the canvas (and thus its
			// repaint callback) is destroyed.
			struct CanvasPaintState
			{
				lua_State* L;
				int ref;
				CanvasContext ctx;

				CanvasPaintState(lua_State* _L, const int _ref) : L(_L), ref(_ref) {}
				~CanvasPaintState()
				{
					if (ref != LUA_NOREF)
						luaL_unref(L, LUA_REGISTRYINDEX, ref);
				}

				void paint(juce::Graphics& _g)
				{
					ctx.begin(_g);
					lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
					LuaType<CanvasContext>::push(L, &ctx, false);
					if (lua_pcall(L, 1, 0, 0) != 0)
					{
						Log::Message(Log::LT_WARNING, "Canvas paint function error: %s", lua_tostring(L, -1));
						lua_pop(L, 1);
					}
					ctx.end();
				}
			};
		}

		int CanvassetPaintFunction(lua_State* L, Canvas* canvas)
		{
			RMLUI_CHECK_OBJ(canvas);
			luaL_checktype(L, 1, LUA_TFUNCTION);

			lua_pushvalue(L, 1);
			const int ref = luaL_ref(L, LUA_REGISTRYINDEX);

			auto state = std::make_shared<CanvasPaintState>(L, ref);

			// Default to a fresh frame each paint; authors can opt into a
			// persistent canvas via setClearEveryFrame(false).
			canvas->setClearEveryFrame(true);
			canvas->setRepaintGraphicsCallback([state](juce::Image&, juce::Graphics& _g) { state->paint(_g); });
			return 0;
		}

		int Canvasrepaint(lua_State* L, Canvas* canvas)
		{
			RMLUI_CHECK_OBJ(canvas);
			canvas->repaint();
			return 0;
		}

		int CanvassetClearEveryFrame(lua_State* L, Canvas* canvas)
		{
			RMLUI_CHECK_OBJ(canvas);
			canvas->setClearEveryFrame(RMLUI_CHECK_BOOL(L, 1));
			return 0;
		}

		RegType<Canvas> CanvasMethods[] = {
			RMLUI_LUAMETHOD(Canvas, setPaintFunction),
			RMLUI_LUAMETHOD(Canvas, repaint),
			RMLUI_LUAMETHOD(Canvas, setClearEveryFrame),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasGetters[] = { { nullptr, nullptr } };
		luaL_Reg CanvasSetters[] = { { nullptr, nullptr } };

		template <>
		void ExtraInit<Canvas>(lua_State* L, const int metatable_index)
		{
			// inherit from Element
			ExtraInit<Element>(L, metatable_index);
			LuaType<Element>::_regfunctions(L, metatable_index, metatable_index - 1);
			AddTypeToElementAsTable<Canvas>(L);
		}
		RMLUI_LUATYPE_DEFINE(Canvas)
	}
}

namespace juceRmlUi
{
	void registerCanvasLua(lua_State* _L)
	{
		if (!_L)
			return;
		Rml::Lua::LuaType<ElemCanvas>::Register(_L);
		Rml::Lua::LuaType<Context2D>::Register(_L);
	}
}
