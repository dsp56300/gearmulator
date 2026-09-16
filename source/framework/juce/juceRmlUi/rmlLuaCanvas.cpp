#include "rmlLuaCanvas.h"

#include "rmlElemCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>

#include "RmlUi/Core/Log.h"
#include "RmlUi/Lua/LuaType.h"
#include "RmlUi/Lua/Utilities.h"	// AddTypeToElementAsTable

#include "Lua/Element.h"			// ExtraInit<Element>, LuaType<Element> (from RmlUi/Source)

namespace Rml
{
	namespace Lua
	{
		// Lua-visible type names are derived from these aliases:
		//   Canvas         -> the <canvas> element, cast via Element.As.Canvas(el)
		//   CanvasContext  -> the 2D drawing context passed to the paint function
		//   CanvasGradient -> a gradient made by createLinearGradient / createRadialGradient
		//
		// Method functions receive their Lua arguments starting at stack index 1
		// (the LuaType thunk removes 'self' and passes it as the C++ pointer).
		// Property getters/setters are not thunked: self is at index 1, the
		// assigned value at index 2.
		using Canvas = juceRmlUi::ElemCanvas;
		using CanvasContext = juceRmlUi::Context2D;
		using CanvasGradient = juceRmlUi::CanvasGradient;
		using CanvasStyle = juceRmlUi::CanvasStyle;

		namespace
		{
			std::optional<juce::Colour> parseCssColour(const juce::String& _in)
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
						return {};
					}
				}

				if (s.startsWithIgnoreCase("rgb"))
				{
					const auto open = s.indexOfChar('(');
					const auto close = s.lastIndexOfChar(')');
					if (open < 0 || close < open)
						return {};

					juce::StringArray parts;
					parts.addTokens(s.substring(open + 1, close), ",", "");
					parts.trim();

					if (parts.size() < 3)
						return {};

					const auto r = static_cast<uint8_t>(juce::jlimit(0, 255, parts[0].getIntValue()));
					const auto g = static_cast<uint8_t>(juce::jlimit(0, 255, parts[1].getIntValue()));
					const auto b = static_cast<uint8_t>(juce::jlimit(0, 255, parts[2].getIntValue()));
					const auto a = parts.size() >= 4
						? static_cast<uint8_t>(juce::jlimit(0, 255, juce::roundToInt(parts[3].getFloatValue() * 255.0f)))
						: static_cast<uint8_t>(255);
					return juce::Colour(r, g, b, a);
				}

				return {};
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

			float numberArg(lua_State* L, const int _index)
			{
				return static_cast<float>(luaL_checknumber(L, _index));
			}

			// HTML5 rectangles may have a negative width or height
			juce::Rectangle<float> rectArgs(lua_State* L, const int _first)
			{
				const auto x = numberArg(L, _first);
				const auto y = numberArg(L, _first + 1);
				const auto w = numberArg(L, _first + 2);
				const auto h = numberArg(L, _first + 3);
				return { juce::Point<float>(x, y), juce::Point<float>(x + w, y + h) };
			}

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

			// needs at least one stop
			juce::ColourGradient toColourGradient(const CanvasGradient::Data& _d)
			{
				juce::ColourGradient g;
				g.point1 = _d.start;
				g.point2 = _d.end;
				g.isRadial = false;

				// juce needs a colour at 0, HTML5 extends the first and the last stop to the ends
				g.addColour(0.0, _d.stops.front().second);
				for (const auto& [offset, colour] : _d.stops)
					g.addColour(offset, colour);
				g.addColour(1.0, _d.stops.back().second);
				return g;
			}

			// An HTML5 radial gradient runs between two circles: a pixel takes the colour at the largest w for which the
			// circle interpolated between them has a positive radius and passes through the pixel, and stays transparent
			// if there is none. juce only knows gradients around one centre, so the colours are computed here.
			juce::Image renderRadialGradient(const CanvasGradient::Data& _d, const juce::Rectangle<int>& _area)
			{
				constexpr int lutSize = 1024;
				juce::PixelARGB lut[lutSize];
				toColourGradient(_d).createLookupTable(lut, lutSize);

				juce::Image image(juce::Image::ARGB, _area.getWidth(), _area.getHeight(), true);
				const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);

				const double cx = _d.end.x - _d.start.x;
				const double cy = _d.end.y - _d.start.y;
				const double r0 = _d.startRadius;
				const double dr = _d.endRadius - _d.startRadius;
				const double a = cx * cx + cy * cy - dr * dr;

				for (int y = 0; y < _area.getHeight(); ++y)
				{
					const double py = _area.getY() + y + 0.5 - _d.start.y;

					for (int x = 0; x < _area.getWidth(); ++x)
					{
						const double px = _area.getX() + x + 0.5 - _d.start.x;

						// |p - w*c| = r0 + w*dr  ->  a*w^2 - 2*b*w + c = 0
						const double b = px * cx + py * cy + r0 * dr;
						const double c = px * px + py * py - r0 * r0;

						double w;

						if (std::abs(a) < 1e-9)
						{
							if (b == 0.0)
								continue;
							w = c / (2.0 * b);
						}
						else
						{
							const double discriminant = b * b - a * c;
							if (discriminant < 0.0)
								continue;
							const double root = std::sqrt(discriminant);
							const double w0 = (b - root) / a;
							const double w1 = (b + root) / a;
							w = std::max(w0, w1);
							if (r0 + w * dr <= 0.0)
								w = std::min(w0, w1);
						}

						if (r0 + w * dr <= 0.0)
							continue;

						const auto index = juce::roundToInt(std::clamp(w, 0.0, 1.0) * (lutSize - 1));
						reinterpret_cast<juce::PixelARGB*>(pixels.getPixelPointer(x, y))->set(lut[index]);
					}
				}

				return image;
			}

			// Makes the style the fill of the graphics for the given area. Returns false if it paints nothing.
			bool setFill(juce::Graphics& _g, const juce::Rectangle<int>& _area, const CanvasStyle& _style)
			{
				if (!_style.gradient)
				{
					_g.setColour(_style.colour);
					return true;
				}

				const auto& d = *_style.gradient;

				// HTML5 paints nothing with a gradient that has no stops or no extent
				if (d.stops.empty())
					return false;

				if (!d.radial)
				{
					if (d.start == d.end)
						return false;
					_g.setGradientFill(toColourGradient(d));
					return true;
				}

				if (d.start == d.end && juce::exactlyEqual(d.startRadius, d.endRadius))
					return false;

				const auto offset = juce::AffineTransform::translation(static_cast<float>(_area.getX()), static_cast<float>(_area.getY()));
				_g.setFillType(juce::FillType(renderRadialGradient(d, _area), offset));
				return true;
			}

			void fillShape(const CanvasContext* _c, const juce::Path& _shape, const CanvasStyle& _style)
			{
				auto* g = gfx(_c);
				if (!g)
					return;

				const auto area = _shape.getBounds().getSmallestIntegerContainer().getIntersection(g->getClipBounds());
				if (area.isEmpty() || !setFill(*g, area, _style))
					return;

				g->fillPath(_shape);
			}

			void strokeShape(const CanvasContext* _c, const juce::Path& _shape)
			{
				if (!gfx(_c))
					return;

				juce::Path outline;
				juce::PathStrokeType(_c->lineWidth, _c->lineJoin, _c->lineCap).createStrokedPath(outline, _shape);
				fillShape(_c, outline, _c->strokeStyle);
			}

			constexpr std::pair<const char*, juce::PathStrokeType::JointStyle> g_lineJoins[] =
			{
				{ "miter", juce::PathStrokeType::mitered },
				{ "round", juce::PathStrokeType::curved },
				{ "bevel", juce::PathStrokeType::beveled },
			};

			constexpr std::pair<const char*, juce::PathStrokeType::EndCapStyle> g_lineCaps[] =
			{
				{ "butt", juce::PathStrokeType::butt },
				{ "round", juce::PathStrokeType::rounded },
				{ "square", juce::PathStrokeType::square },
			};

			template <typename Names, typename T>
			void pushName(lua_State* L, const Names& _names, const T _value)
			{
				for (const auto& [name, value] : _names)
				{
					if (value == _value)
					{
						lua_pushstring(L, name);
						return;
					}
				}
				lua_pushnil(L);
			}

			// HTML5 ignores a value that is none of the names
			template <typename Names, typename T>
			void setFromName(lua_State* L, const Names& _names, T& _value)
			{
				if (lua_type(L, 2) != LUA_TSTRING)
					return;

				const auto* text = lua_tostring(L, 2);

				for (const auto& [name, value] : _names)
				{
					if (std::strcmp(text, name) == 0)
						_value = value;
				}
			}
		}

		// ---------------------------------------------------------------------
		// CanvasGradient (HTML5 CanvasGradient); method args start at stack index 1
		// ---------------------------------------------------------------------

		int CanvasGradientaddColorStop(lua_State* L, CanvasGradient* gradient)
		{
			const auto offset = numberArg(L, 1);
			const auto* text = luaL_checkstring(L, 2);

			// HTML5 throws for both
			if (!(offset >= 0.0f && offset <= 1.0f))
				return luaL_error(L, "addColorStop: offset %f is outside of 0..1", static_cast<double>(offset));

			const auto colour = parseCssColour(text);
			if (!colour)
				return luaL_error(L, "addColorStop: '%s' is not a colour", text);

			auto& stops = gradient->data->stops;
			const auto pos = std::upper_bound(stops.begin(), stops.end(), offset, [](const float _offset, const auto& _stop)
			{
				return _offset < _stop.first;
			});
			stops.insert(pos, { offset, *colour });
			return 0;
		}

		RegType<CanvasGradient> CanvasGradientMethods[] = {
			RMLUI_LUAMETHOD(CanvasGradient, addColorStop),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasGradientGetters[] = { { nullptr, nullptr } };
		luaL_Reg CanvasGradientSetters[] = { { nullptr, nullptr } };

		template <>
		void ExtraInit<CanvasGradient>(lua_State*, int)
		{
		}
		RMLUI_LUATYPE_DEFINE(CanvasGradient)

		namespace
		{
			int pushGradient(lua_State* L, const std::shared_ptr<CanvasGradient::Data>& _data)
			{
				auto* gradient = new CanvasGradient();
				gradient->data = _data;
				LuaType<CanvasGradient>::push(L, gradient, true);
				return 1;
			}

			int pushStyle(lua_State* L, const CanvasStyle& _style)
			{
				if (_style.gradient)
					return pushGradient(L, _style.gradient);

				lua_pushstring(L, colourToCss(_style.colour).toRawUTF8());
				return 1;
			}

			// HTML5 ignores a value that is neither a colour nor a gradient
			void setStyle(lua_State* L, CanvasStyle& _style)
			{
				if (const auto* gradient = static_cast<CanvasGradient**>(luaL_testudata(L, 2, GetTClassName<CanvasGradient>())))
				{
					_style.gradient = (*gradient)->data;
					return;
				}

				if (lua_type(L, 2) != LUA_TSTRING)
					return;

				if (const auto colour = parseCssColour(lua_tostring(L, 2)))
				{
					_style.colour = *colour;
					_style.gradient.reset();
				}
			}
		}

		// ---------------------------------------------------------------------
		// CanvasContext (HTML5 CanvasRenderingContext2D subset)
		// method args start at stack index 1
		// ---------------------------------------------------------------------

		int CanvasContextfillRect(lua_State* L, CanvasContext* c)
		{
			juce::Path shape;
			shape.addRectangle(rectArgs(L, 1));
			fillShape(c, shape, c->fillStyle);
			return 0;
		}

		int CanvasContextstrokeRect(lua_State* L, CanvasContext* c)
		{
			juce::Path shape;
			shape.addRectangle(rectArgs(L, 1));
			strokeShape(c, shape);
			return 0;
		}

		int CanvasContextclearRect(lua_State* L, CanvasContext* c)
		{
			const auto area = rectArgs(L, 1).toNearestIntEdges();

			auto* g = gfx(c);
			if (!g)
				return 0;

			// HTML5 sets the pixels inside the clip to transparent black, which no blending fill does
			g->saveState();
			g->setColour(juce::Colours::transparentBlack);
			g->getInternalContext().fillRect(area, true);
			g->restoreState();
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
			fillShape(c, c->path(), c->fillStyle);
			return 0;
		}

		int CanvasContextstroke(lua_State*, CanvasContext* c)
		{
			strokeShape(c, c->path());
			return 0;
		}

		// lasts until the paint function returns
		int CanvasContextclip(lua_State*, CanvasContext* c)
		{
			if (auto* g = gfx(c))
				g->reduceClipRegion(c->path());
			return 0;
		}

		int CanvasContextcreateLinearGradient(lua_State* L, CanvasContext*)
		{
			const auto x0 = numberArg(L, 1);
			const auto y0 = numberArg(L, 2);
			const auto x1 = numberArg(L, 3);
			const auto y1 = numberArg(L, 4);

			auto data = std::make_shared<CanvasGradient::Data>();
			data->start = { x0, y0 };
			data->end = { x1, y1 };
			return pushGradient(L, data);
		}

		int CanvasContextcreateRadialGradient(lua_State* L, CanvasContext*)
		{
			const auto x0 = numberArg(L, 1);
			const auto y0 = numberArg(L, 2);
			const auto r0 = numberArg(L, 3);
			const auto x1 = numberArg(L, 4);
			const auto y1 = numberArg(L, 5);
			const auto r1 = numberArg(L, 6);

			// HTML5 throws
			if (r0 < 0.0f || r1 < 0.0f)
				return luaL_error(L, "createRadialGradient: a radius is negative");

			auto data = std::make_shared<CanvasGradient::Data>();
			data->radial = true;
			data->start = { x0, y0 };
			data->end = { x1, y1 };
			data->startRadius = r0;
			data->endRadius = r1;
			return pushGradient(L, data);
		}

		// getters / setters (dot-syntax properties): self at 1, value at 2

		int CanvasContextGetAttrfillStyle(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			return pushStyle(L, c->fillStyle);
		}
		int CanvasContextSetAttrfillStyle(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			setStyle(L, c->fillStyle);
			return 0;
		}

		int CanvasContextGetAttrstrokeStyle(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			return pushStyle(L, c->strokeStyle);
		}
		int CanvasContextSetAttrstrokeStyle(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			setStyle(L, c->strokeStyle);
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
			const auto width = luaL_checknumber(L, 2);
			// HTML5 ignores zero, negative, infinite and NaN widths
			if (width > 0.0 && std::isfinite(width))
				c->lineWidth = static_cast<float>(width);
			return 0;
		}

		int CanvasContextGetAttrlineJoin(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			pushName(L, g_lineJoins, c->lineJoin);
			return 1;
		}
		int CanvasContextSetAttrlineJoin(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			setFromName(L, g_lineJoins, c->lineJoin);
			return 0;
		}

		int CanvasContextGetAttrlineCap(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			pushName(L, g_lineCaps, c->lineCap);
			return 1;
		}
		int CanvasContextSetAttrlineCap(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			setFromName(L, g_lineCaps, c->lineCap);
			return 0;
		}

		int CanvasContextGetAttrcanvas(lua_State* L);	// needs the Canvas type, defined below

		RegType<CanvasContext> CanvasContextMethods[] = {
			RMLUI_LUAMETHOD(CanvasContext, fillRect),
			RMLUI_LUAMETHOD(CanvasContext, strokeRect),
			RMLUI_LUAMETHOD(CanvasContext, clearRect),
			RMLUI_LUAMETHOD(CanvasContext, beginPath),
			RMLUI_LUAMETHOD(CanvasContext, closePath),
			RMLUI_LUAMETHOD(CanvasContext, moveTo),
			RMLUI_LUAMETHOD(CanvasContext, lineTo),
			RMLUI_LUAMETHOD(CanvasContext, rect),
			RMLUI_LUAMETHOD(CanvasContext, arc),
			RMLUI_LUAMETHOD(CanvasContext, ellipse),
			RMLUI_LUAMETHOD(CanvasContext, fill),
			RMLUI_LUAMETHOD(CanvasContext, stroke),
			RMLUI_LUAMETHOD(CanvasContext, clip),
			RMLUI_LUAMETHOD(CanvasContext, createLinearGradient),
			RMLUI_LUAMETHOD(CanvasContext, createRadialGradient),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasContextGetters[] = {
			RMLUI_LUAGETTER(CanvasContext, fillStyle),
			RMLUI_LUAGETTER(CanvasContext, strokeStyle),
			RMLUI_LUAGETTER(CanvasContext, lineWidth),
			RMLUI_LUAGETTER(CanvasContext, lineJoin),
			RMLUI_LUAGETTER(CanvasContext, lineCap),
			RMLUI_LUAGETTER(CanvasContext, canvas),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasContextSetters[] = {
			RMLUI_LUASETTER(CanvasContext, fillStyle),
			RMLUI_LUASETTER(CanvasContext, strokeStyle),
			RMLUI_LUASETTER(CanvasContext, lineWidth),
			RMLUI_LUASETTER(CanvasContext, lineJoin),
			RMLUI_LUASETTER(CanvasContext, lineCap),
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

				CanvasPaintState(lua_State* _L, const int _ref, Canvas* _canvas) : L(_L), ref(_ref), ctx(_canvas) {}
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

			auto state = std::make_shared<CanvasPaintState>(L, ref, canvas);

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

		// HTML5 ctx.canvas, the element the context draws on
		int CanvasContextGetAttrcanvas(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			LuaType<Canvas>::push(L, c->canvas(), false);
			return 1;
		}
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
		Rml::Lua::LuaType<CanvasGradient>::Register(_L);
	}
}
