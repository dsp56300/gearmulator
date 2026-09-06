#include "rmlLuaCanvas.h"

#include "rmlElemCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <cstring>
#include <optional>
#include <initializer_list>
#include <cstdio>
#include <memory>

#include "juce_graphics/juce_graphics.h"
#include "RmlUi/Core/CoreInstance.h"
#include "RmlUi/Core/Log.h"
#include "RmlUi/Core/PropertyParser.h"
#include "RmlUi/Core/StyleSheetSpecification.h"
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
		// what Lua holds: html5 lets a script keep a gradient, so Lua and the drawing state share ownership
		struct CanvasGradient { std::shared_ptr<juceRmlUi::CanvasGradient> gradient; };

		namespace
		{
			// rgba() keeps its html5 0-1 alpha, unlike RCSS, so skins must not go via the css parser
			bool parseCanvasColour(const juce::String& _in, juce::Colour& _out)
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
							_out = juce::Colour(r, g, b);
							return true;
						}
					case 6:	// #rrggbb
						_out = juce::Colour(static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v), static_cast<uint8_t>(255));
						return true;
					case 8:	// #rrggbbaa
						_out = juce::Colour(static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v));
						return true;
					default:
						return false;
					}
				}

				if (s.startsWithIgnoreCase("rgb"))
				{
					const auto open = s.indexOfChar('(');
					const auto close = s.lastIndexOfChar(')');
					if (open < 0 || close < open)
						return false;

					// css color 4 uses spaces and a slash before alpha, so both count as delimiters
					juce::StringArray parts;
					parts.addTokens(s.substring(open + 1, close), ", /\t", "");
					parts.removeEmptyStrings();
					parts.trim();

					if (parts.size() < 3)
						return false;

					const auto component = [](const juce::String& _v)
					{
						const auto scale = _v.endsWithChar('%') ? 2.55f : 1.0f;
						return static_cast<uint8_t>(juce::jlimit(0, 255, juce::roundToInt(_v.getFloatValue() * scale)));
					};
					const auto r = component(parts[0]);
					const auto g = component(parts[1]);
					const auto b = component(parts[2]);
					// alpha is a 0-1 number, or a percentage when written css color 4 style
					const auto alphaScale = parts.size() >= 4 && parts[3].endsWithChar('%') ? 0.01f : 1.0f;
					const auto a = parts.size() >= 4
						? static_cast<uint8_t>(juce::jlimit(0, 255, juce::roundToInt(parts[3].getFloatValue() * alphaScale * 255.0f)))
						: static_cast<uint8_t>(255);
					_out = juce::Colour(r, g, b, a);
					return true;
				}

				return false;
			}

			// unrecognised forms go to RCSS's parser, so named colours and hsl() work as in a stylesheet
			std::optional<juce::Colour> parseCssColour(Rml::CoreInstance* _coreInstance, const juce::String& _in)
			{
				juce::Colour parsed;

				if (parseCanvasColour(_in, parsed))
					return parsed;

				if (!_coreInstance)
					return {};

				auto* parser = _coreInstance->styleSheetSpecification->GetParser("color");

				if (!parser)
					return {};

				Rml::Property property;

				if (!parser->ParseValue(property, _in.trim().toStdString(), Rml::ParameterMap()))
					return {};

				const auto c = property.Get<Rml::Colourb>(*_coreInstance);

				return juce::Colour(c.red, c.green, c.blue, c.alpha);
			}

			juce::String colourToCss(const juce::Colour _c)
			{
				// html5 reads a translucent colour back as rgba() with a 0-1 alpha, never as 8 digit hex
				if (_c.getAlpha() != 255)
				{
					return "rgba(" + juce::String(_c.getRed()) + ", " + juce::String(_c.getGreen()) + ", " +
						juce::String(_c.getBlue()) + ", " + juce::String(_c.getFloatAlpha(), 3) + ")";
				}

				const auto hex2 = [](const uint8_t _v) { return juce::String::toHexString(&_v, 1, 0).paddedLeft('0', 2); };
				return "#" + hex2(_c.getRed()) + hex2(_c.getGreen()) + hex2(_c.getBlue());
			}

			// html5 fill rules: "nonzero" is the default, "evenodd" makes overlapping sub-paths cancel
			void applyFillRule(CanvasContext* _c, lua_State* _L, const int _arg)
			{
				const auto* rule = lua_isstring(_L, _arg) ? lua_tostring(_L, _arg) : nullptr;
				_c->path().setUsingNonZeroWinding(!rule || std::strcmp(rule, "evenodd") != 0);
			}

			juce::Graphics* gfx(const CanvasContext* _c) { return _c ? _c->graphics() : nullptr; }

			// LuaType::check compares no metatable, so any userdata passes and a stray one gets cast
			template <typename T> T* checkType(lua_State* _L, const int _index)
			{
				auto* ud = luaL_testudata(_L, _index, GetTClassName<T>());
				return ud ? *static_cast<T**>(ud) : nullptr;
			}

			// cubics keep a transformed arc in the current sub-path; angles from +x, not juce's 12 o'clock
			void addArc(CanvasContext* _c, const float _x, const float _y, const float _rx, const float _ry, const float _rot, const float _a0, const float _a1, const bool _ccw)
			{
				// html5: the ccw flag picks direction of travel, not which arc, so winding reverses too
				auto sweep = _a1 - _a0;

				if (_ccw)
				{
					// spec is ambiguous at 2pi; browsers keep the full circle, not an empty arc
					if (std::abs(sweep) >= juce::MathConstants<float>::twoPi - 1.0e-4f)
					{
						sweep = -juce::MathConstants<float>::twoPi;
					}
					else
					{
						while (sweep > 0.0f)
							sweep -= juce::MathConstants<float>::twoPi;
						sweep = std::max(sweep, -juce::MathConstants<float>::twoPi);
					}
				}
				else if (std::abs(sweep) >= juce::MathConstants<float>::twoPi - 1.0e-4f)
				{
					sweep = juce::MathConstants<float>::twoPi;
				}
				else
				{
					// past 2pi*2^23 a turn rounds to nothing in float, so an unguarded loop never ends
					while (sweep < 0.0f)
						sweep += juce::MathConstants<float>::twoPi;
					sweep = std::min(sweep, juce::MathConstants<float>::twoPi);
				}

				const auto ellipse = juce::AffineTransform::rotation(_rot).translated(_x, _y);
				const auto pointAt = [&](const float _angle)
				{
					return _c->transformed(juce::Point<float>(_rx * std::cos(_angle), _ry * std::sin(_angle)).transformedBy(ellipse));
				};

				const auto start = pointAt(_a0);

				if (!_c->pathStarted())
					_c->path().startNewSubPath(start);
				else
					_c->path().lineTo(start);	// html5 joins the arc to the current point with a line

				// a cubic approximates an arc well below a quarter turn, so split into that many pieces
				const auto segments = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (juce::MathConstants<float>::halfPi * 0.5f))));
				const auto step = sweep / static_cast<float>(segments);
				const auto handle = 4.0f / 3.0f * std::tan(step * 0.25f);

				for (int i = 0; i < segments; ++i)
				{
					const auto s0 = _a0 + step * static_cast<float>(i);
					const auto s1 = s0 + step;

					// the control points lie along the tangents at each end of the segment
					const auto t0 = juce::Point<float>(-_rx * std::sin(s0), _ry * std::cos(s0)) * handle;
					const auto t1 = juce::Point<float>(-_rx * std::sin(s1), _ry * std::cos(s1)) * handle;

					const auto p0 = juce::Point<float>(_rx * std::cos(s0), _ry * std::sin(s0));
					const auto p1 = juce::Point<float>(_rx * std::cos(s1), _ry * std::sin(s1));

					_c->path().cubicTo(
						_c->transformed((p0 + t0).transformedBy(ellipse)),
						_c->transformed((p1 - t1).transformedBy(ellipse)),
						_c->transformed(p1.transformedBy(ellipse)));
				}

				// a full turn ends where it began, and left open the stroker butt caps both ends rather
				// than joining them, halving the coverage along the seam. closing joins them; the fresh
				// sub-path then puts the current point back where html5 leaves it, at the arc's end
				if (std::abs(sweep) >= juce::MathConstants<float>::twoPi - 1.0e-4f)
				{
					_c->path().closeSubPath();
					_c->path().startNewSubPath(start);
				}

				_c->pathStarted() = true;
			}
		}

		// ---------------------------------------------------------------------
		// CanvasContext (HTML5 CanvasRenderingContext2D subset)
		// method args start at stack index 1
		// ---------------------------------------------------------------------

		// html5 ignores non-finite args; -Ofast folds std::isfinite() to true, so check the exponent bits
		// (the helpers here are file local: external linkage would put these names in every juceRmlUi TU)
		static bool isFiniteValue(const float _v)
		{
			uint32_t bits;
			std::memcpy(&bits, &_v, sizeof(bits));
			return (bits & 0x7f800000u) != 0x7f800000u;	// exponent all ones means inf or nan
		}

		static bool allFinite(std::initializer_list<float> _values)
		{
			for (const auto v : _values)
			{
				if (!isFiniteValue(v))
					return false;
			}
			return true;
		}


		int CanvasContexttranslate(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			if (!allFinite({x, y}))
				return 0;
			// html5 applies the new operation in the current user space, so it comes before the existing ctm
			c->state.transform = juce::AffineTransform::translation(x, y).followedBy(c->state.transform);
			return 0;
		}

		int CanvasContextrotate(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto a = static_cast<float>(luaL_checknumber(L, 1));
			if (!allFinite({a}))
				return 0;
			c->state.transform = juce::AffineTransform::rotation(a).followedBy(c->state.transform);
			return 0;
		}

		int CanvasContextscale(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			if (!allFinite({x, y}))
				return 0;
			c->state.transform = juce::AffineTransform::scale(x, y).followedBy(c->state.transform);
			return 0;
		}

		int CanvasContexttransform(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			// html5 argument order is a, b, c, d, e, f by column; juce takes rows
			const auto a = static_cast<float>(luaL_checknumber(L, 1));
			const auto b = static_cast<float>(luaL_checknumber(L, 2));
			const auto cc = static_cast<float>(luaL_checknumber(L, 3));
			const auto d = static_cast<float>(luaL_checknumber(L, 4));
			const auto e = static_cast<float>(luaL_checknumber(L, 5));
			const auto f = static_cast<float>(luaL_checknumber(L, 6));
			if (!allFinite({a, b, cc, d, e, f}))
				return 0;
			c->state.transform = juce::AffineTransform(a, cc, e, b, d, f).followedBy(c->state.transform);
			return 0;
		}

		int CanvasContextsetTransform(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto a = static_cast<float>(luaL_checknumber(L, 1));
			const auto b = static_cast<float>(luaL_checknumber(L, 2));
			const auto cc = static_cast<float>(luaL_checknumber(L, 3));
			const auto d = static_cast<float>(luaL_checknumber(L, 4));
			const auto e = static_cast<float>(luaL_checknumber(L, 5));
			const auto f = static_cast<float>(luaL_checknumber(L, 6));
			if (!allFinite({a, b, cc, d, e, f}))
				return 0;
			c->state.transform = juce::AffineTransform(a, cc, e, b, d, f);	// replaces rather than concatenates
			return 0;
		}

		int CanvasContextresetTransform(lua_State*, CanvasContext* c)
		{
			if (c)
				c->state.transform = juce::AffineTransform();
			return 0;
		}

		// CanvasGradient, as returned by createLinearGradient / createRadialGradient

		int CanvasGradientaddColorStop(lua_State* L, CanvasGradient* g)
		{
			RMLUI_CHECK_OBJ(g);
			auto& gradient = *g->gradient;
			const auto offset = luaL_checknumber(L, 1);

			// html5 throws rather than ignoring; a lua error is the same contract - the author sees the typo
			if (!(offset >= 0.0 && offset <= 1.0))	// written to reject a nan offset too
				return luaL_error(L, "addColorStop: offset %f is outside 0..1", offset);

			// the same vocabulary fillStyle takes
			const auto* text = luaL_checkstring(L, 2);
			const auto colour = parseCssColour(gradient.coreInstance, text);

			if (!colour)
				return luaL_error(L, "addColorStop: '%s' is not a colour", text);

			// html5 inserts in offset order, and after any stop already at the same offset
			using Stop = juceRmlUi::CanvasGradient::Stop;
			const auto at = std::upper_bound(gradient.stops.begin(), gradient.stops.end(), offset,
				[](const double _offset, const Stop& _stop) { return _offset < _stop.offset; });
			gradient.stops.insert(at, {offset, *colour});
			++gradient.stopsVersion;	// the built gradient and the raster are both derived from these
			return 0;
		}

		RegType<CanvasGradient> CanvasGradientMethods[] = {
			RMLUI_LUAMETHOD(CanvasGradient, addColorStop),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasGradientGetters[] = { { nullptr, nullptr } };
		luaL_Reg CanvasGradientSetters[] = { { nullptr, nullptr } };

		template <> void ExtraInit<CanvasGradient>(lua_State*, int) {}
		RMLUI_LUATYPE_DEFINE(CanvasGradient)

		int CanvasContextcreateLinearGradient(lua_State* L, CanvasContext* c)
		{
			RMLUI_CHECK_OBJ(c);
			auto gradient = std::make_shared<juceRmlUi::CanvasGradient>();
			gradient->kind = juceRmlUi::CanvasGradient::Kind::Linear;
			gradient->from = { static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)) };
			gradient->to   = { static_cast<float>(luaL_checknumber(L, 3)), static_cast<float>(luaL_checknumber(L, 4)) };
			if (!allFinite({gradient->from.x, gradient->from.y, gradient->to.x, gradient->to.y}))
				return 0;	// html5 throws here; a nan would otherwise reach the raster's ramp index
			gradient->degenerate = gradient->from.getDistanceFrom(gradient->to) < 1.0e-6f;
			gradient->coreInstance = c->coreInstance();
			LuaType<CanvasGradient>::push(L, new CanvasGradient{gradient}, true);
			return 1;
		}

		int CanvasContextcreateConicGradient(lua_State* L, CanvasContext* c)
		{
			RMLUI_CHECK_OBJ(c);
			auto gradient = std::make_shared<juceRmlUi::CanvasGradient>();
			gradient->kind = juceRmlUi::CanvasGradient::Kind::Conic;
			// html5 argument order is (startAngle, x, y)
			gradient->conicAngle = static_cast<float>(luaL_checknumber(L, 1));
			gradient->from = { static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)) };
			if (!allFinite({gradient->conicAngle, gradient->from.x, gradient->from.y}))
				return 0;
			gradient->coreInstance = c->coreInstance();
			LuaType<CanvasGradient>::push(L, new CanvasGradient{gradient}, true);
			return 1;
		}

		int CanvasContextcreateRadialGradient(lua_State* L, CanvasContext* c)
		{
			RMLUI_CHECK_OBJ(c);
			auto gradient = std::make_shared<juceRmlUi::CanvasGradient>();
			gradient->kind = juceRmlUi::CanvasGradient::Kind::Radial;
			// html5 has two circles, juce one: take the larger and reverse the stops if that is the first
			const juce::Point<float> c0{ static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)) };
			const auto r0 = static_cast<float>(luaL_checknumber(L, 3));
			const juce::Point<float> c1{ static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_checknumber(L, 5)) };
			const auto r1 = static_cast<float>(luaL_checknumber(L, 6));

			if (!allFinite({c0.x, c0.y, r0, c1.x, c1.y, r1}))
				return 0;
			gradient->circle0 = c0; gradient->circle1 = c1;
			gradient->radius0 = r0; gradient->radius1 = r1;
			gradient->degenerate = c0.getDistanceFrom(c1) < 1.0e-6f && std::abs(r0 - r1) < 1.0e-6f;
			gradient->reversed = r0 > r1;
			gradient->from     = gradient->reversed ? c0 : c1;
			gradient->to       = gradient->from.translated(gradient->reversed ? r0 : r1, 0.0f);

			// html5 ramps between the circles, not from the centre: stops start at inner/outer, not at 0
			const auto outer = std::max(r0, r1);
			gradient->innerRatio = outer > 0.0f ? juce::jlimit(0.0f, 1.0f, std::min(r0, r1) / outer) : 0.0f;
			gradient->coreInstance = c->coreInstance();
			LuaType<CanvasGradient>::push(L, new CanvasGradient{gradient}, true);
			return 1;
		}

		int CanvasContextsave(lua_State*, CanvasContext* c)
		{
			if (c)
				c->save();
			return 0;
		}

		int CanvasContextrestore(lua_State*, CanvasContext* c)
		{
			if (c)
				c->restore();
			return 0;
		}

		// juce::Path::addRectangle starts at the bottom left corner and html5's rect() at the top left.
		// Both wind clockwise, so fills are unaffected, but a dash pattern walks the perimeter from the
		// start point - so the corner it begins at decides the phase on every edge.
		static void addHtml5Rect(juce::Path& _path, const juce::Rectangle<float>& _r)
		{
			_path.startNewSubPath(_r.getX(), _r.getY());
			_path.lineTo(_r.getRight(), _r.getY());
			_path.lineTo(_r.getRight(), _r.getBottom());
			_path.lineTo(_r.getX(), _r.getBottom());
			_path.closeSubPath();
		}

		// html5 normalises a negative extent and ignores a non-finite one; shared so the guard is not missed
		static bool readRect(lua_State* _L, juce::Rectangle<float>& _out)
		{
			const auto x = static_cast<float>(luaL_checknumber(_L, 1));
			const auto y = static_cast<float>(luaL_checknumber(_L, 2));
			const auto w = static_cast<float>(luaL_checknumber(_L, 3));
			const auto h = static_cast<float>(luaL_checknumber(_L, 4));

			if (!allFinite({x, y, w, h}))
				return false;

			_out = juce::Rectangle<float>(std::min(x, x + w), std::min(y, y + h), std::abs(w), std::abs(h));
			return true;
		}

		int CanvasContextclearRect(lua_State* L, CanvasContext* c)
		{
			juce::Rectangle<float> area;
			if (!c || !c->image() || !readRect(L, area))
				return 0;

			// juce clears whole pixels, so round outwards
			const auto rect = area.getSmallestIntegerContainer().getIntersection(c->image()->getBounds());

			if (!rect.isEmpty())
				c->image()->clear(rect);
			return 0;
		}

		int CanvasContextfillRect(lua_State* L, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			// juce::Path::addRectangle normalises for us, but Graphics::fillRect just draws nothing
			juce::Rectangle<float> area;
			if (!readRect(L, area))
				return 0;

			if (c->hasShadow())
			{
				juce::Path shadowRect;
				shadowRect.addRectangle(area);
				shadowRect.applyTransform(c->state.transform);
				c->drawShadow(*g, shadowRect);
			}

			c->applyFill(*g, area.transformedBy(c->state.transform).getSmallestIntegerContainer());

			// fillRect is the hottest call in skins, and an untransformed rect takes juce's fast fill path
			if (c->state.transform.isIdentity())
			{
				g->fillRect(area);
				return 0;
			}

			juce::Path rect;
			rect.addRectangle(area);
			g->fillPath(rect, c->state.transform);
			return 0;
		}

		int CanvasContextstrokeRect(lua_State* L, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			juce::Rectangle<float> area;
			if (!readRect(L, area))
				return 0;

			// html5 centres the stroke on the rectangle's edge, so the path is the rectangle itself
			juce::Path rect;
			addHtml5Rect(rect, area);

			// the rectangle is already in user space, which is where strokeOutlineOf wants it
			const auto outline = c->strokeOutlineOf(rect);
			c->drawShadow(*g, outline);
			c->applyStroke(*g, outline.getBounds().getSmallestIntegerContainer());
			g->fillPath(outline);
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
			if (!allFinite({x, y}))	// html5 ignores a geometry call with a non-finite argument
				return 0;
			c->path().startNewSubPath(c->transformed(x, y));
			c->pathStarted() = true;
			return 0;
		}

		int CanvasContextlineTo(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			if (!allFinite({x, y}))	// html5 ignores a geometry call with a non-finite argument
				return 0;
			if (!c->pathStarted())
				c->path().startNewSubPath(c->transformed(x, y));	// HTML5: lineTo with no current point acts as moveTo
			else
				c->path().lineTo(c->transformed(x, y));
			c->pathStarted() = true;
			return 0;
		}

		int CanvasContextquadraticCurveTo(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto cx = static_cast<float>(luaL_checknumber(L, 1));
			const auto cy = static_cast<float>(luaL_checknumber(L, 2));
			const auto x = static_cast<float>(luaL_checknumber(L, 3));
			const auto y = static_cast<float>(luaL_checknumber(L, 4));
			if (!allFinite({cx, cy, x, y}))	// html5 ignores a geometry call with a non-finite argument
				return 0;
			if (!c->pathStarted())
				c->path().startNewSubPath(c->transformed(cx, cy));	// html5: no current point means the first control point becomes it
			c->path().quadraticTo(c->transformed(cx, cy), c->transformed(x, y));
			c->pathStarted() = true;
			return 0;
		}

		int CanvasContextbezierCurveTo(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto c1x = static_cast<float>(luaL_checknumber(L, 1));
			const auto c1y = static_cast<float>(luaL_checknumber(L, 2));
			const auto c2x = static_cast<float>(luaL_checknumber(L, 3));
			const auto c2y = static_cast<float>(luaL_checknumber(L, 4));
			const auto x = static_cast<float>(luaL_checknumber(L, 5));
			const auto y = static_cast<float>(luaL_checknumber(L, 6));
			if (!allFinite({c1x, c1y, c2x, c2y, x, y}))	// html5 ignores a geometry call with a non-finite argument
				return 0;
			if (!c->pathStarted())
				c->path().startNewSubPath(c->transformed(c1x, c1y));
			c->path().cubicTo(c->transformed(c1x, c1y), c->transformed(c2x, c2y), c->transformed(x, y));
			c->pathStarted() = true;
			return 0;
		}

		int CanvasContextroundRect(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			juce::Rectangle<float> area;
			if (!readRect(L, area))
				return 0;

			// html5 also takes per corner radii; juce rounds them alike, so the first entry is used
			auto r = 0.0f;
			if (lua_istable(L, 5))
			{
				lua_rawgeti(L, 5, 1);
				r = static_cast<float>(lua_tonumber(L, -1));
				lua_pop(L, 1);
			}
			else if (lua_gettop(L) >= 5)
			{
				r = static_cast<float>(luaL_checknumber(L, 5));
			}

			if (!isFiniteValue(r))
				return 0;

			if (r < 0.0f)		// html5 throws here too, a RangeError rather than arc's IndexSizeError
				return luaL_error(L, "roundRect: radius %f is negative", r);

			// a radius past half the shorter side collapses to a stadium, matching html5's scaling rule
			const auto radius = juce::jlimit(0.0f, std::min(area.getWidth(), area.getHeight()) * 0.5f, r);
			juce::Path rounded;
			rounded.addRoundedRectangle(area, radius);
			c->path().addPath(rounded, c->state.transform);
			c->pathStarted() = true;
			return 0;
		}

		int CanvasContextarcTo(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			const auto x1 = static_cast<float>(luaL_checknumber(L, 1));
			const auto y1 = static_cast<float>(luaL_checknumber(L, 2));
			const auto x2 = static_cast<float>(luaL_checknumber(L, 3));
			const auto y2 = static_cast<float>(luaL_checknumber(L, 4));
			const auto radius = static_cast<float>(luaL_checknumber(L, 5));
			if (!allFinite({x1, y1, x2, y2, radius}))	// html5 ignores a geometry call with a non-finite argument
				return 0;

			if (radius < 0.0f)		// html5 throws an IndexSizeError here; a lua error is the same contract
				return luaL_error(L, "arcTo: radius %f is negative", radius);

			const juce::Point<float> p1(x1, y1);
			const juce::Point<float> p2(x2, y2);

			// html5: with no current point, arcTo begins the path at the first control point
			if (!c->pathStarted())
			{
				c->path().startNewSubPath(c->transformed(x1, y1));
				c->pathStarted() = true;
				return 0;
			}

			// the geometry is in user space, so the current point comes back through the inverse transform
			if (c->state.transform.isSingularity())
				return 0;

			const auto p0 = c->path().getCurrentPosition().transformedBy(c->state.transform.inverted());

			auto v1 = p0 - p1;
			auto v2 = p2 - p1;
			const auto len1 = v1.getDistanceFromOrigin();
			const auto len2 = v2.getDistanceFromOrigin();

			// degenerate cases all collapse to a straight line to the first control point
			if (len1 < 1.0e-6f || len2 < 1.0e-6f || radius < 1.0e-6f)
			{
				c->path().lineTo(c->transformed(x1, y1));
				return 0;
			}

			v1 /= len1;
			v2 /= len2;

			const auto theta = std::acos(juce::jlimit(-1.0f, 1.0f, v1.x * v2.x + v1.y * v2.y));

			if (theta < 1.0e-4f || theta > juce::MathConstants<float>::pi - 1.0e-4f)	// collinear
			{
				c->path().lineTo(c->transformed(x1, y1));
				return 0;
			}

			const auto tangentDistance = radius / std::tan(theta * 0.5f);
			const auto t1 = p1 + v1 * tangentDistance;
			const auto t2 = p1 + v2 * tangentDistance;

			auto bisector = v1 + v2;
			bisector /= bisector.getDistanceFromOrigin();
			const auto centre = p1 + bisector * (radius / std::sin(theta * 0.5f));

			const auto a0 = std::atan2(t1.y - centre.y, t1.x - centre.x);
			const auto a1 = std::atan2(t2.y - centre.y, t2.x - centre.x);

			// take the minor arc, which is the one html5 draws
			auto sweep = a1 - a0;
			while (sweep < 0.0f)
				sweep += juce::MathConstants<float>::twoPi;

			c->path().lineTo(c->transformed(t1.x, t1.y));
			addArc(c, centre.x, centre.y, radius, radius, 0.0f, a0, a1, sweep > juce::MathConstants<float>::pi);
			return 0;
		}

		int CanvasContextrect(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			juce::Rectangle<float> area;
			if (!readRect(L, area))
				return 0;
			juce::Path rect;
			addHtml5Rect(rect, area);
			c->path().addPath(rect, c->state.transform);
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
			if (!allFinite({x, y, r, a0, a1}))	// html5 ignores a geometry call with a non-finite argument
				return 0;
			if (r < 0.0f)		// as in arcTo, html5 throws rather than drawing a mirrored arc
				return luaL_error(L, "arc: radius %f is negative", r);
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
			if (!allFinite({x, y, rx, ry, rot, a0, a1}))	// html5 ignores a geometry call with a non-finite argument
				return 0;
			if (rx < 0.0f || ry < 0.0f)		// as in arc(), html5 throws rather than drawing a mirrored arc
				return luaL_error(L, "ellipse: radii %f, %f must not be negative", rx, ry);
			const bool ccw = lua_gettop(L) >= 8 && lua_toboolean(L, 8) != 0;
			addArc(c, x, y, rx, ry, rot, a0, a1, ccw);
			return 0;
		}

		int CanvasContextisPointInPath(lua_State* L, CanvasContext* c)
		{
			RMLUI_CHECK_OBJ(c);
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));
			applyFillRule(c, L, 3);
			// html5: these coordinates are unaffected by the ctm, and the path is already in device space
			lua_pushboolean(L, c->path().contains(x, y) ? 1 : 0);
			return 1;
		}

		int CanvasContextisPointInStroke(lua_State* L, CanvasContext* c)
		{
			RMLUI_CHECK_OBJ(c);
			const auto x = static_cast<float>(luaL_checknumber(L, 1));
			const auto y = static_cast<float>(luaL_checknumber(L, 2));

			// test the outline stroke() would paint, so dashes and degenerate caps are honoured here too
			lua_pushboolean(L, c->buildStrokeOutline().contains(x, y) ? 1 : 0);
			return 1;
		}

		int CanvasContextclip(lua_State* L, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			applyFillRule(c, L, 1);
			g->reduceClipRegion(c->path());
			return 0;
		}

		int CanvasContextfill(lua_State* L, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			applyFillRule(c, L, 1);
			c->drawShadow(*g, c->path());
			c->applyFill(*g, c->path().getBounds().getSmallestIntegerContainer());
			g->fillPath(c->path());
			return 0;
		}

		int CanvasContextstroke(lua_State*, CanvasContext* c)
		{
			auto* g = gfx(c);
			if (!g)
				return 0;
			const auto outline = c->buildStrokeOutline();
			c->drawShadow(*g, outline);
			c->applyStroke(*g, outline.getBounds().getSmallestIntegerContainer());
			g->fillPath(outline);
			return 0;
		}

		int CanvasContextsetLineDash(lua_State* L, CanvasContext* c)
		{
			if (!c)
				return 0;
			luaL_checktype(L, 1, LUA_TTABLE);

			std::vector<float> dashes;
			const auto count = static_cast<int>(lua_rawlen(L, 1));

			for (int i = 1; i <= count; ++i)
			{
				lua_rawgeti(L, 1, i);
				const auto v = static_cast<float>(lua_tonumber(L, -1));
				lua_pop(L, 1);
				// written so a nan fails it too: juce's dash walker makes no progress on one and spins
				if (!(isFiniteValue(v) && v >= 0.0f))	// html5 ignores the list if any entry is bad
					return 0;
				dashes.push_back(v);
			}

			// html5 repeats an odd length list once to make it even, which juce also requires
			if (dashes.size() & 1)
			{
				const auto once = dashes;	// inserting a range of a container into itself is undefined
				dashes.insert(dashes.end(), once.begin(), once.end());
			}

			// a list that is all zeroes would make no progress along the path
			if (std::all_of(dashes.begin(), dashes.end(), [](const float _v) { return _v <= 0.0f; }))
				dashes.clear();

			c->state.lineDash = std::move(dashes);
			return 0;
		}

		int CanvasContextgetLineDash(lua_State* L, CanvasContext* c)
		{
			RMLUI_CHECK_OBJ(c);
			lua_newtable(L);
			for (size_t i = 0; i < c->state.lineDash.size(); ++i)
			{
				lua_pushnumber(L, c->state.lineDash[i]);
				lua_rawseti(L, -2, static_cast<int>(i) + 1);
			}
			return 1;
		}

		// getters / setters (dot-syntax properties): self at 1, value at 2

		// six properties differing only in member and accept rule, so the rules cannot drift apart
#define CANVAS_FLOAT_PROPERTY(name, accept)                             \
		int CanvasContextGetAttr##name(lua_State* L)                    \
		{                                                               \
			auto* c = LuaType<CanvasContext>::check(L, 1);              \
			RMLUI_CHECK_OBJ(c);                                         \
			lua_pushnumber(L, c->state.name);                           \
			return 1;                                                   \
		}                                                               \
		int CanvasContextSetAttr##name(lua_State* L)                    \
		{                                                               \
			auto* c = LuaType<CanvasContext>::check(L, 1);              \
			RMLUI_CHECK_OBJ(c);                                         \
			const auto v = static_cast<float>(luaL_checknumber(L, 2));  \
			if (accept)                                                 \
				c->state.name = v;                                      \
			return 0;                                                   \
		}

		// html5 ignores a width that is not a positive finite number
		CANVAS_FLOAT_PROPERTY(lineWidth, isFiniteValue(v) && v > 0.0f)
		// html5 ignores an alpha outside 0..1
		CANVAS_FLOAT_PROPERTY(globalAlpha, isFiniteValue(v) && v >= 0.0f && v <= 1.0f)
		// html5 ignores a non-finite value, keeping the current one
		CANVAS_FLOAT_PROPERTY(lineDashOffset, isFiniteValue(v))
		// html5 ignores a non-finite or negative value
		CANVAS_FLOAT_PROPERTY(shadowBlur, isFiniteValue(v) && v >= 0.0f)
		// html5 ignores a non-finite value; a negative offset is legal
		CANVAS_FLOAT_PROPERTY(shadowOffsetX, isFiniteValue(v))
		// html5 ignores a non-finite value; a negative offset is legal
		CANVAS_FLOAT_PROPERTY(shadowOffsetY, isFiniteValue(v))

#undef CANVAS_FLOAT_PROPERTY

		// two properties differing only in the pair of state members they touch, so they cannot drift apart
#define CANVAS_STYLE_PROPERTY(name, colourMember, gradientMember)                              \
		int CanvasContextGetAttr##name(lua_State* L)                                           \
		{                                                                                      \
			auto* c = LuaType<CanvasContext>::check(L, 1);                                     \
			RMLUI_CHECK_OBJ(c);                                                                \
			lua_pushstring(L, colourToCss(c->state.colourMember).toRawUTF8());                 \
			return 1;                                                                          \
		}                                                                                      \
		int CanvasContextSetAttr##name(lua_State* L)                                           \
		{                                                                                      \
			auto* c = LuaType<CanvasContext>::check(L, 1);                                     \
			RMLUI_CHECK_OBJ(c);                                                                \
			/* either a css colour string or a gradient object */                              \
			if (!lua_isstring(L, 2))                                                           \
			{                                                                                  \
				if (auto* handle = checkType<CanvasGradient>(L, 2))                            \
					c->state.gradientMember = handle->gradient;                                \
				return 0;                                                                      \
			}                                                                                  \
			c->state.gradientMember.reset();                                                   \
			const auto parsed = parseCssColour(c->coreInstance(), luaL_checkstring(L, 2));     \
			c->state.colourMember = parsed.value_or(c->state.colourMember);                    \
			return 0;                                                                          \
		}

		CANVAS_STYLE_PROPERTY(fillStyle, fillColour, fillGradient)
		CANVAS_STYLE_PROPERTY(strokeStyle, strokeColour, strokeGradient)

#undef CANVAS_STYLE_PROPERTY

		// html5 reaches the element through the context, and its size is the backing store being drawn into
		int CanvasContextGetAttrcanvas(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			if (auto* element = c->element())
				LuaType<Canvas>::push(L, element, false);
			else
				lua_pushnil(L);	// outside a paint there is no canvas to hand out
			return 1;
		}

		// shadowColor takes no gradient, so it is the plain half of the pair above rather than a third case
		int CanvasContextGetAttrshadowColor(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			lua_pushstring(L, colourToCss(c->state.shadowColour).toRawUTF8());
			return 1;
		}
		int CanvasContextSetAttrshadowColor(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			c->state.shadowColour = parseCssColour(c->coreInstance(), luaL_checkstring(L, 2)).value_or(c->state.shadowColour);
			return 0;
		}

		int CanvasContextGetAttrlineCap(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			switch (c->state.lineCap)
			{
			case juce::PathStrokeType::rounded:	lua_pushstring(L, "round"); break;
			case juce::PathStrokeType::square:	lua_pushstring(L, "square"); break;
			default:							lua_pushstring(L, "butt"); break;
			}
			return 1;
		}
		int CanvasContextSetAttrlineCap(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			const juce::String v(luaL_checkstring(L, 2));
			if (v == "round")
				c->state.lineCap = juce::PathStrokeType::rounded;
			else if (v == "square")
				c->state.lineCap = juce::PathStrokeType::square;
			else if (v == "butt")
				c->state.lineCap = juce::PathStrokeType::butt;	// html5 ignores an unknown value, keeping the current one
			return 0;
		}

		int CanvasContextGetAttrlineJoin(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			switch (c->state.lineJoin)
			{
			case juce::PathStrokeType::curved:	lua_pushstring(L, "round"); break;
			case juce::PathStrokeType::beveled:	lua_pushstring(L, "bevel"); break;
			default:							lua_pushstring(L, "miter"); break;
			}
			return 1;
		}
		int CanvasContextSetAttrlineJoin(lua_State* L)
		{
			auto* c = LuaType<CanvasContext>::check(L, 1);
			RMLUI_CHECK_OBJ(c);
			const juce::String v(luaL_checkstring(L, 2));
			if (v == "round")
				c->state.lineJoin = juce::PathStrokeType::curved;
			else if (v == "bevel")
				c->state.lineJoin = juce::PathStrokeType::beveled;
			else if (v == "miter")
				c->state.lineJoin = juce::PathStrokeType::mitered;
			return 0;
		}

		// unprovided Canvas 2D api, present only so using one warns instead of doing nothing silently

#define CANVAS_UNSUPPORTED_METHOD(name)                                     \
		int CanvasContext##name(lua_State*, CanvasContext* c)               \
		{                                                                   \
			if (c)                                                          \
				c->warnUnsupported(#name "()");                             \
			return 0;                                                       \
		}

#define CANVAS_UNSUPPORTED_PROPERTY(name)                                   \
		int CanvasContextSetAttr##name(lua_State* L)                        \
		{                                                                   \
			if (auto* c = LuaType<CanvasContext>::check(L, 1))              \
				c->warnUnsupported(#name);                                  \
			return 0;                                                       \
		}

		// no blend mode control exists in juce::Graphics, which composites source-over only
		CANVAS_UNSUPPORTED_PROPERTY(globalCompositeOperation)
		// juce hardcodes its miter limit rather than exposing one
		CANVAS_UNSUPPORTED_PROPERTY(miterLimit)
		CANVAS_UNSUPPORTED_PROPERTY(filter)
		// text and images are deliberately left to real RmlUi elements, which do them better
		CANVAS_UNSUPPORTED_PROPERTY(font)
		CANVAS_UNSUPPORTED_PROPERTY(textAlign)
		CANVAS_UNSUPPORTED_PROPERTY(textBaseline)
		CANVAS_UNSUPPORTED_PROPERTY(direction)
		CANVAS_UNSUPPORTED_PROPERTY(imageSmoothingEnabled)

		CANVAS_UNSUPPORTED_METHOD(fillText)
		CANVAS_UNSUPPORTED_METHOD(strokeText)
		CANVAS_UNSUPPORTED_METHOD(measureText)
		CANVAS_UNSUPPORTED_METHOD(drawImage)
		CANVAS_UNSUPPORTED_METHOD(createPattern)
		CANVAS_UNSUPPORTED_METHOD(createImageData)
		CANVAS_UNSUPPORTED_METHOD(getImageData)
		CANVAS_UNSUPPORTED_METHOD(putImageData)

#undef CANVAS_UNSUPPORTED_METHOD
#undef CANVAS_UNSUPPORTED_PROPERTY

		RegType<CanvasContext> CanvasContextMethods[] = {
			RMLUI_LUAMETHOD(CanvasContext, createLinearGradient),
			RMLUI_LUAMETHOD(CanvasContext, createRadialGradient),
			RMLUI_LUAMETHOD(CanvasContext, createConicGradient),
			RMLUI_LUAMETHOD(CanvasContext, translate),
			RMLUI_LUAMETHOD(CanvasContext, rotate),
			RMLUI_LUAMETHOD(CanvasContext, scale),
			RMLUI_LUAMETHOD(CanvasContext, transform),
			RMLUI_LUAMETHOD(CanvasContext, setTransform),
			RMLUI_LUAMETHOD(CanvasContext, resetTransform),
			RMLUI_LUAMETHOD(CanvasContext, save),
			RMLUI_LUAMETHOD(CanvasContext, restore),
			RMLUI_LUAMETHOD(CanvasContext, clearRect),
			RMLUI_LUAMETHOD(CanvasContext, fillRect),
			RMLUI_LUAMETHOD(CanvasContext, strokeRect),
			RMLUI_LUAMETHOD(CanvasContext, beginPath),
			RMLUI_LUAMETHOD(CanvasContext, closePath),
			RMLUI_LUAMETHOD(CanvasContext, moveTo),
			RMLUI_LUAMETHOD(CanvasContext, lineTo),
			RMLUI_LUAMETHOD(CanvasContext, rect),
			RMLUI_LUAMETHOD(CanvasContext, roundRect),
			RMLUI_LUAMETHOD(CanvasContext, quadraticCurveTo),
			RMLUI_LUAMETHOD(CanvasContext, bezierCurveTo),
			RMLUI_LUAMETHOD(CanvasContext, arc),
			RMLUI_LUAMETHOD(CanvasContext, arcTo),
			RMLUI_LUAMETHOD(CanvasContext, ellipse),
			RMLUI_LUAMETHOD(CanvasContext, fill),
			RMLUI_LUAMETHOD(CanvasContext, stroke),
			RMLUI_LUAMETHOD(CanvasContext, clip),
			RMLUI_LUAMETHOD(CanvasContext, isPointInPath),
			RMLUI_LUAMETHOD(CanvasContext, isPointInStroke),
			RMLUI_LUAMETHOD(CanvasContext, setLineDash),
			RMLUI_LUAMETHOD(CanvasContext, getLineDash),
			// the unsupported ones, which only warn
			RMLUI_LUAMETHOD(CanvasContext, fillText),
			RMLUI_LUAMETHOD(CanvasContext, strokeText),
			RMLUI_LUAMETHOD(CanvasContext, measureText),
			RMLUI_LUAMETHOD(CanvasContext, drawImage),
			RMLUI_LUAMETHOD(CanvasContext, createPattern),
			RMLUI_LUAMETHOD(CanvasContext, createImageData),
			RMLUI_LUAMETHOD(CanvasContext, getImageData),
			RMLUI_LUAMETHOD(CanvasContext, putImageData),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasContextGetters[] = {
			RMLUI_LUAGETTER(CanvasContext, fillStyle),
			RMLUI_LUAGETTER(CanvasContext, strokeStyle),
			RMLUI_LUAGETTER(CanvasContext, lineWidth),
			RMLUI_LUAGETTER(CanvasContext, lineCap),
			RMLUI_LUAGETTER(CanvasContext, lineJoin),
			RMLUI_LUAGETTER(CanvasContext, globalAlpha),
			RMLUI_LUAGETTER(CanvasContext, lineDashOffset),
			RMLUI_LUAGETTER(CanvasContext, shadowColor),
			RMLUI_LUAGETTER(CanvasContext, shadowBlur),
			RMLUI_LUAGETTER(CanvasContext, shadowOffsetX),
			RMLUI_LUAGETTER(CanvasContext, shadowOffsetY),
			RMLUI_LUAGETTER(CanvasContext, canvas),
			{ nullptr, nullptr },
		};

		luaL_Reg CanvasContextSetters[] = {
			RMLUI_LUASETTER(CanvasContext, fillStyle),
			RMLUI_LUASETTER(CanvasContext, strokeStyle),
			RMLUI_LUASETTER(CanvasContext, lineWidth),
			RMLUI_LUASETTER(CanvasContext, lineCap),
			RMLUI_LUASETTER(CanvasContext, lineJoin),
			RMLUI_LUASETTER(CanvasContext, globalAlpha),
			RMLUI_LUASETTER(CanvasContext, lineDashOffset),
			RMLUI_LUASETTER(CanvasContext, shadowColor),
			RMLUI_LUASETTER(CanvasContext, shadowBlur),
			RMLUI_LUASETTER(CanvasContext, shadowOffsetX),
			RMLUI_LUASETTER(CanvasContext, shadowOffsetY),
			RMLUI_LUASETTER(CanvasContext, globalCompositeOperation),
			RMLUI_LUASETTER(CanvasContext, miterLimit),
			RMLUI_LUASETTER(CanvasContext, filter),
			RMLUI_LUASETTER(CanvasContext, font),
			RMLUI_LUASETTER(CanvasContext, textAlign),
			RMLUI_LUASETTER(CanvasContext, textBaseline),
			RMLUI_LUASETTER(CanvasContext, direction),
			RMLUI_LUASETTER(CanvasContext, imageSmoothingEnabled),
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
				CoreInstance& coreInstance;
				Canvas& canvas;	// owns the callback that owns this, so it outlives every paint
				CanvasContext ctx;
				bool errorReported = false;	// a paint that raises raises every frame, so only the first is logged

				CanvasPaintState(lua_State* _L, const int _ref, CoreInstance& _coreInstance, Canvas& _canvas)
					: L(_L), ref(_ref), coreInstance(_coreInstance), canvas(_canvas) {}
				~CanvasPaintState()
				{
					if (ref != LUA_NOREF)
						luaL_unref(L, LUA_REGISTRYINDEX, ref);
				}

				void paint(juce::Image& _image, juce::Graphics& _g)
				{
					ctx.begin(_image, _g, coreInstance, canvas);
					lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
					LuaType<CanvasContext>::push(L, &ctx, false);
					if (lua_pcall(L, 1, 0, 0) != 0)
					{
						// the message would otherwise repeat at the frame rate. reporting resumes when the
						// canvas is rebuilt, which is what reloading the skin after a fix does anyway
						if (!errorReported)
						{
							errorReported = true;
							Log::Message(Log::LT_WARNING, "Canvas paint function error: %s"
								" (further errors from this canvas are suppressed until the skin is reloaded)",
								lua_tostring(L, -1));
						}
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

			auto state = std::make_shared<CanvasPaintState>(L, ref, canvas->GetCoreInstance(), *canvas);

			// Default to a fresh frame each paint; authors can opt into a
			// persistent canvas via setClearEveryFrame(false).
			canvas->setClearEveryFrame(true);
			canvas->setRepaintGraphicsCallback([state](juce::Image& _image, juce::Graphics& _g) { state->paint(_image, _g); });
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

		// html5's canvas.width / canvas.height: the backing store, in device pixels, not the css size
		int CanvasGetAttrwidth(lua_State* L)
		{
			auto* canvas = LuaType<Canvas>::check(L, 1);
			RMLUI_CHECK_OBJ(canvas);
			lua_pushnumber(L, canvas->getTextureSize().x);
			return 1;
		}
		int CanvasGetAttrheight(lua_State* L)
		{
			auto* canvas = LuaType<Canvas>::check(L, 1);
			RMLUI_CHECK_OBJ(canvas);
			lua_pushnumber(L, canvas->getTextureSize().y);
			return 1;
		}

		luaL_Reg CanvasGetters[] = {
			RMLUI_LUAGETTER(Canvas, width),
			RMLUI_LUAGETTER(Canvas, height),
			{ nullptr, nullptr },
		};
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
	// One box pass as a running sum: the window advances by adding the sample entering it and
	// subtracting the one leaving, so the cost per sample does not depend on the window width.
	// Zero outside the run is the right edge rule here, the mask being empty beyond the blur margin.
	static void boxPass(const uint8_t* _src, uint8_t* _dst, const int _count, const int _srcStride,
		const int _left, const int _right)
	{
		const auto width = static_cast<uint32_t>(_left + _right + 1);
		const auto sample = [&](const int _i) -> uint32_t
		{
			return _i >= 0 && _i < _count ? _src[static_cast<size_t>(_i) * static_cast<size_t>(_srcStride)] : 0u;
		};

		uint32_t sum = 0;
		for (int i = -_left; i <= _right; ++i)
			sum += sample(i);

		for (int x = 0; x < _count; ++x)
		{
			_dst[x] = static_cast<uint8_t>((sum + width / 2) / width);
			sum += sample(x + _right + 1);
			sum -= sample(x - _left);
		}
	}

	// Three box passes approximate a gaussian. The widths follow svg's feGaussianBlur, which is what
	// browsers use for shadowBlur, so matching it is also what keeps the parity cases passing. juce
	// instead repeats a fixed 3 tap box 2*radius times, making its pass count grow with the radius.
	static void blurSingleChannel(juce::Image& _image, const float _sigma)
	{
		const juce::Image::BitmapData bm(_image, juce::Image::BitmapData::readWrite);
		const auto w = bm.width, h = bm.height;

		const auto d = static_cast<int>(std::floor(
			_sigma * 3.0f * std::sqrt(juce::MathConstants<float>::twoPi) / 4.0f + 0.5f));

		if (d < 1 || w < 1 || h < 1)
			return;

		// odd d: three boxes of width d. even d: two of width d offset opposite ways, then one of d+1.
		const int lefts [3] = { d / 2, (d & 1) ? d / 2 : d / 2 - 1, d / 2 };
		const int rights[3] = { (d & 1) ? d / 2 : d / 2 - 1, d / 2, d / 2 };

		std::vector<uint8_t> scratch(static_cast<size_t>(juce::jmax(w, h)));

		for (int y = 0; y < h; ++y)
		{
			auto* row = bm.getLinePointer(y);
			for (int p = 0; p < 3; ++p)
			{
				boxPass(row, scratch.data(), w, 1, lefts[p], rights[p]);
				std::memcpy(row, scratch.data(), static_cast<size_t>(w));
			}
		}

		for (int x = 0; x < w; ++x)
		{
			auto* col = bm.getPixelPointer(x, 0);
			for (int p = 0; p < 3; ++p)
			{
				boxPass(col, scratch.data(), h, bm.lineStride, lefts[p], rights[p]);
				for (int y = 0; y < h; ++y)
					col[static_cast<size_t>(y) * static_cast<size_t>(bm.lineStride)] = scratch[static_cast<size_t>(y)];
			}
		}
	}

	// html5 caps a zero-length sub-path (round dot, square square); juce's stroker drops it, so add by hand
	// (file local: it has one caller, and external linkage here would put the name in every juceRmlUi TU)
	static void addDegenerateCaps(const juce::PathStrokeType::EndCapStyle _cap, const juce::Path& _source, const float _radius, juce::Path& _dest)
	{
		if (_cap == juce::PathStrokeType::butt || _radius <= 0.0f)
			return;

		juce::Path::Iterator it(_source);
		bool haveStart = false;
		bool degenerate = false;
		bool haveSegment = false;
		juce::Point<float> start;

		// a zero radius arc still emits curves, so every one of a segment's points must sit on the start
		const auto offStart = [&](const float _x, const float _y) { return start.getDistanceFrom({_x, _y}) > 1.0e-4f; };

		const auto flush = [&]()
		{
			// html5 caps a sub-path whose segments collapsed to a point, but not one with no segments
			if (!haveStart || !degenerate || !haveSegment)
				return;
			if (_cap == juce::PathStrokeType::rounded)
				_dest.addEllipse(start.x - _radius, start.y - _radius, _radius * 2.0f, _radius * 2.0f);
			else
				_dest.addRectangle(start.x - _radius, start.y - _radius, _radius * 2.0f, _radius * 2.0f);
		};

		while (it.next())
		{
			if (it.elementType == juce::Path::Iterator::startNewSubPath)
			{
				flush();
				start = { it.x1, it.y1 };
				haveStart = true;
				degenerate = true;
				haveSegment = false;
			}
			else if (it.elementType == juce::Path::Iterator::lineTo)
			{
				haveSegment = true;
				if (offStart(it.x1, it.y1))
					degenerate = false;
			}
			else if (it.elementType == juce::Path::Iterator::quadraticTo)
			{
				haveSegment = true;
				if (offStart(it.x1, it.y1) || offStart(it.x2, it.y2))
					degenerate = false;
			}
			else if (it.elementType == juce::Path::Iterator::cubicTo)
			{
				haveSegment = true;
				if (offStart(it.x1, it.y1) || offStart(it.x2, it.y2) || offStart(it.x3, it.y3))
					degenerate = false;
			}
		}
		flush();
	}

	void Context2D::apply(juce::Graphics& _g, const std::shared_ptr<CanvasGradient>& _gradient, const juce::Colour _colour,
		const juce::Rectangle<int> _deviceBounds) const
	{
		if (!_gradient)
		{
			_g.setColour(_colour);
			return;
		}

		// html5 paints nothing for a zero-length linear or an identical-circle radial; juce would fill flat
		if (_gradient->degenerate)
		{
			_g.setColour(juce::Colours::transparentBlack);
			return;
		}

		if (!_gradient->needsRaster())
		{
			// gradient coordinates are in user space, so they follow the same transform as the geometry
			_g.setFillType(juce::FillType(_gradient->buildCached(state.globalAlpha)).transformed(state.transform));
			return;
		}

		// a conic gradient, or a focal radial, has no juce equivalent and is evaluated into an image
		auto area = _g.getClipBounds();

		if (!_deviceBounds.isEmpty())
			area = area.getIntersection(_deviceBounds);

		if (area.isEmpty())
		{
			_g.setColour(juce::Colours::transparentBlack);
			return;
		}

		// one sample per pixel: 256x fewer moved the median 0.23ms, inside the noise, so it bought nothing
		const auto& image = _gradient->rasteriseCached(area, state.transform, state.globalAlpha);

		// the raster is 1:1 at an integer offset, so nearest sampling is exact rather than merely close
		_g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
		_g.setFillType(juce::FillType(image, juce::AffineTransform::translation(
			static_cast<float>(area.getX()), static_cast<float>(area.getY()))));
	}

	void Context2D::drawShadow(juce::Graphics& _g, const juce::Path& _path) const
	{
		if (!hasShadow())
			return;

		const auto colour = state.shadowColour.withMultipliedAlpha(state.globalAlpha);
		const juce::Point<int> offset(juce::roundToInt(state.shadowOffsetX), juce::roundToInt(state.shadowOffsetY));

		const auto sigma = state.shadowBlur * 0.5f;	// html5, straight in - no fitted constant needed

		// too little to blur, so the shadow is just an offset copy
		if (sigma < 0.5f)
		{
			_g.setColour(colour);
			_g.fillPath(_path, juce::AffineTransform::translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
			return;
		}

		// a gaussian is spent by about three sigma, which is also what makes the mask zero at the edges
		const auto margin = juce::roundToInt(std::ceil(3.0f * sigma)) + 1;
		const auto area = (_path.getBounds().getSmallestIntegerContainer() + offset).expanded(margin)
			.getIntersection(_g.getClipBounds().expanded(margin));

		if (area.getWidth() < 1 || area.getHeight() < 1)
			return;

		juce::Image mask(juce::Image::SingleChannel, area.getWidth(), area.getHeight(), true);
		{
			juce::Graphics maskGraphics(mask);
			maskGraphics.setColour(juce::Colours::white);
			maskGraphics.fillPath(_path, juce::AffineTransform::translation(
				static_cast<float>(offset.x - area.getX()), static_cast<float>(offset.y - area.getY())));
		}

		blurSingleChannel(mask, sigma);

		_g.setColour(colour);
		_g.drawImageAt(mask, area.getX(), area.getY(), true);
	}

	// one outline for drawing and hit testing, so a dashed line cannot hit test as solid
	juce::Path Context2D::buildStrokeOutline() const
	{
		// the stored path is in device space, so it comes back to user space to be stroked
		if (state.transform.isSingularity())
			return {};	// html5 ignores drawing operations under a non-invertible ctm

		if (state.transform.isIdentity())
			return strokeOutlineOf(m_path);	// user and device space coincide, so skip both traversals

		auto user = m_path;
		user.applyTransform(state.transform.inverted());
		return strokeOutlineOf(user);
	}

	juce::Path Context2D::strokeOutlineOf(const juce::Path& _userPath) const
	{
		// html5's pen is a circle in user space, so stroke there and map back; juce strokes a scalar width
		const juce::PathStrokeType type(state.lineWidth, state.lineJoin, state.lineCap);

		// curves flatten before that mapping, so ask for accuracy in proportion to the magnification
		const auto accuracy = juce::jlimit(1.0f, 100.0f, transformScale());

		juce::Path outline;

		if (state.lineDash.empty())
		{
			type.createStrokedPath(outline, _userPath, {}, accuracy);
		}
		else
		{
			const auto pattern = dashPattern();
			type.createDashedStroke(outline, _userPath, pattern.data(), static_cast<int>(pattern.size()), {}, accuracy);
		}

		addDegenerateCaps(state.lineCap, _userPath, state.lineWidth * 0.5f, outline);

		if (!state.transform.isIdentity())
			outline.applyTransform(state.transform);
		return outline;
	}

	// createDashedStroke has no phase argument, so the pattern starts lineDashOffset along itself
	std::vector<float> Context2D::dashPattern() const
	{
		const auto& dashes = state.lineDash;

		if (dashes.empty())
			return {};

		auto total = 0.0f;
		for (const auto d : dashes)
			total += d;

		if (total <= 0.0f)
			return {};

		// html5 offsets forwards; a negative offset walks backwards through the repeating pattern
		auto offset = std::fmod(state.lineDashOffset, total);
		if (offset < 0.0f)
			offset += total;

		if (offset <= 0.0f)
			return dashes;

		// find where the offset lands, and how much of that element is left
		size_t index = 0;
		while (offset >= dashes[index])
		{
			offset -= dashes[index];
			index = (index + 1) % dashes.size();
		}

		std::vector<float> pattern;
		pattern.reserve(dashes.size() + 2);

		// juce reads on, off, on, off..., so a pattern starting mid-gap needs a leading zero length dash
		if (index & 1)
			pattern.push_back(0.0f);

		pattern.push_back(dashes[index] - offset);

		for (size_t i = 1; i < dashes.size(); ++i)
			pattern.push_back(dashes[(index + i) % dashes.size()]);

		// the consumed part of the first element belongs at the end, so the cycle stays whole
		pattern.push_back(offset);

		// an odd entry count swaps on/off on every repeat, so a trailing zero length gap restores the parity
		if (pattern.size() & 1)
			pattern.push_back(0.0f);

		return pattern;
	}

	// unimplemented api is otherwise a silent no-op; warned once, as paint runs every frame
	void Context2D::warnUnsupported(const char* _name)
	{
		// keyed by the literal's address, so the already-warned path allocates nothing at all
		if (!m_warnedUnsupported.insert(static_cast<const void*>(_name)).second)
			return;

		Rml::Log::Message(Rml::Log::LT_WARNING,
			"Canvas: '%s' is part of the HTML5 canvas API but is not supported here, and has been ignored", _name);
	}

	juce::Colour CanvasGradient::colourAt(double _t, const float _alpha) const
	{
		if (stops.empty())
			return juce::Colours::transparentBlack;

		// no 'reversed' here: that is build()'s fit to juce's one circle; the raster has the true offset
		_t = juce::jlimit(0.0, 1.0, _t);

		// stops are authored in order and clamped at both ends, as html5 requires
		if (_t <= stops.front().offset)
			return stops.front().colour.withMultipliedAlpha(_alpha);
		if (_t >= stops.back().offset)
			return stops.back().colour.withMultipliedAlpha(_alpha);

		for (size_t i = 1; i < stops.size(); ++i)
		{
			const auto& a = stops[i - 1];
			const auto& b = stops[i];

			if (_t > b.offset)
				continue;

			const auto span = b.offset - a.offset;
			const auto u = span > 1.0e-9 ? static_cast<float>((_t - a.offset) / span) : 1.0f;
			return a.colour.interpolatedWith(b.colour, u).withMultipliedAlpha(_alpha);
		}

		return stops.back().colour.withMultipliedAlpha(_alpha);
	}

	juce::Image CanvasGradient::rasterise(const juce::Rectangle<int>& _deviceArea,
		const juce::AffineTransform& _userToDevice, const float _alpha) const
	{
		const auto w = juce::jmax(1, _deviceArea.getWidth());
		const auto h = juce::jmax(1, _deviceArea.getHeight());

		juce::Image img(juce::Image::ARGB, w, h, false);
		const juce::Image::BitmapData bm(img, juce::Image::BitmapData::writeOnly);

		// colourAt walks the stops per sample, so the ramp is evaluated once into a premultiplied table
		constexpr auto rampSize = 1024;
		std::vector<juce::PixelARGB> ramp(rampSize);
		for (int k = 0; k < rampSize; ++k)
			ramp[static_cast<size_t>(k)] = colourAt(static_cast<double>(k) / (rampSize - 1), _alpha).getPixelARGB();

		const juce::PixelARGB clear(0, 0, 0, 0);

		const auto toUser = _userToDevice.inverted();
		const auto dc = circle1 - circle0;
		const auto dr = radius1 - radius0;
		const auto a = dc.x * dc.x + dc.y * dc.y - dr * dr;

		// the map is affine, so step it: two adds per sample instead of a full transform
		const auto origin = juce::Point<float>(
			static_cast<float>(_deviceArea.getX()) + 0.5f,
			static_cast<float>(_deviceArea.getY()) + 0.5f).transformedBy(toUser);
		const auto zero = juce::Point<float>(0.0f, 0.0f).transformedBy(toUser);
		const auto du = juce::Point<float>(1.0f, 0.0f).transformedBy(toUser) - zero;
		const auto dv = juce::Point<float>(0.0f, 1.0f).transformedBy(toUser) - zero;

		for (int j = 0; j < h; ++j)
		{
			auto* row = reinterpret_cast<juce::PixelARGB*>(bm.getLinePointer(j));
			const auto rowStart = origin + dv * static_cast<float>(j);

			for (int i = 0; i < w; ++i)
			{
				const auto u = rowStart + du * static_cast<float>(i);

				double t = 0.0;
				bool paint = true;

				if (kind == Kind::Conic)
				{
					const auto ang = std::atan2(u.y - from.y, u.x - from.x) - conicAngle;
					t = ang / juce::MathConstants<double>::twoPi;
					t -= std::floor(t);	// conicAngle is unbounded, so wrap rather than add a single turn
				}
				else
				{
					// the focal case: solve for the circle in the family that passes through this point
					const auto q = u - circle0;
					const auto b = -2.0f * (q.x * dc.x + q.y * dc.y + radius0 * dr);
					const auto cc = q.x * q.x + q.y * q.y - radius0 * radius0;

					if (std::abs(a) < 1.0e-6f)
					{
						paint = std::abs(b) > 1.0e-9f;
						t = paint ? -cc / b : 0.0;
					}
					else
					{
						const auto disc = b * b - 4.0f * a * cc;
						if (disc < 0.0f)
						{
							paint = false;
						}
						else
						{
							const auto root = std::sqrt(disc);
							const auto t0 = (-b + root) / (2.0f * a);
							const auto t1 = (-b - root) / (2.0f * a);
							// html5 takes the largest omega whose circle still has a positive radius
							t = std::max(t0, t1);
							if (radius0 + t * dr < 0.0f)
								t = std::min(t0, t1);
							paint = radius0 + t * dr >= 0.0f;
						}
					}
				}

				// clamp before scaling: t is unbounded here and casting past INT_MAX is undefined
				const auto index = static_cast<int>(juce::jlimit(0.0, 1.0, t) * (rampSize - 1) + 0.5);
				row[i] = paint ? ramp[static_cast<size_t>(index)] : clear;
			}
		}

		return img;
	}

	// the stops do not change between draws, and a gradient outlives the paint that made it
	const juce::ColourGradient& CanvasGradient::buildCached(const float _alpha) const
	{
		if (m_builtVersion != stopsVersion || m_builtAlpha != _alpha)
		{
			m_builtGradient = build(_alpha);
			m_builtVersion = stopsVersion;
			m_builtAlpha = _alpha;
		}
		return m_builtGradient;
	}

	const juce::Image& CanvasGradient::rasteriseCached(const juce::Rectangle<int>& _deviceArea,
		const juce::AffineTransform& _userToDevice, const float _alpha) const
	{
		const auto same = m_rasterVersion == stopsVersion && m_rasterAlpha == _alpha
			&& m_rasterArea == _deviceArea && m_rasterTransform == _userToDevice;

		if (!same)
		{
			m_raster = rasterise(_deviceArea, _userToDevice, _alpha);
			m_rasterVersion = stopsVersion;
			m_rasterAlpha = _alpha;
			m_rasterArea = _deviceArea;
			m_rasterTransform = _userToDevice;
		}
		return m_raster;
	}

	juce::ColourGradient CanvasGradient::build(const float _alpha) const
	{
		juce::ColourGradient g;
		g.isRadial = kind == Kind::Radial;
		g.point1 = from;
		g.point2 = to;
		g.clearColours();

		// juce needs at least two stops and interpolates between them by proportion
		if (stops.empty())
		{
			g.addColour(0.0, juce::Colours::transparentBlack);
			g.addColour(1.0, juce::Colours::transparentBlack);
			return g;
		}

		if (stops.size() == 1)
		{
			g.addColour(0.0, stops.front().colour.withMultipliedAlpha(_alpha));
			g.addColour(1.0, stops.front().colour.withMultipliedAlpha(_alpha));
			return g;
		}

		// juce's addColour() replaces entry 0 when given proportion zero, so entries must arrive ascending
		std::vector<std::pair<double, juce::Colour>> ordered;
		ordered.reserve(stops.size());

		for (const auto& stop : stops)
		{
			const auto omega = reversed ? 1.0 - stop.offset : stop.offset;
			ordered.emplace_back(innerRatio + omega * (1.0 - innerRatio), stop.colour.withMultipliedAlpha(_alpha));
		}

		std::stable_sort(ordered.begin(), ordered.end(), [](const auto& _a, const auto& _b) { return _a.first < _b.first; });

		// without a colour at 0 juce renormalises and undoes the inner-radius offset; repeat the first one
		if (!ordered.empty() && ordered.front().first > 0.0)
			ordered.insert(ordered.begin(), {0.0, ordered.front().second});

		for (const auto& stop : ordered)
			g.addColour(stop.first, stop.second);

		return g;
	}

	void registerCanvasLua(lua_State* _L)
	{
		if (!_L)
			return;
		Rml::Lua::LuaType<ElemCanvas>::Register(_L);
		Rml::Lua::LuaType<Context2D>::Register(_L);
		Rml::Lua::LuaType<Rml::Lua::CanvasGradient>::Register(_L);
	}
}
