#pragma once

#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "juce_graphics/juce_graphics.h"

struct lua_State;

namespace Rml
{
	class CoreInstance;
}

namespace juceRmlUi
{
	class ElemCanvas;

	// html5 CanvasGradient: stops are kept as authored and the juce gradient built on use, as html5 requires
	class CanvasGradient
	{
	public:
		struct Stop
		{
			double offset;
			juce::Colour colour;
		};

		// linear and concentric radial are native juce gradients; conic and focal radial are rasterised
		enum class Kind { Linear, Radial, Conic };

		Kind kind = Kind::Linear;
		juce::Point<float> from;
		juce::Point<float> to;
		bool reversed = false;	// stops run from the outer circle inwards
		bool degenerate = false;	// html5 paints nothing at all for these
		float innerRatio = 0.0f;	// inner radius over outer, the point the stops start from
		std::vector<Stop> stops;

		// the two circles as authored, kept for the focal case
		juce::Point<float> circle0, circle1;
		float radius0 = 0.0f, radius1 = 0.0f;
		float conicAngle = 0.0f;
		Rml::CoreInstance* coreInstance = nullptr;	// for parsing stop colours, as fillStyle does

		int stopsVersion = 0;

		bool needsRaster() const
		{
			return kind == Kind::Conic ||
				(kind == Kind::Radial && circle0.getDistanceFrom(circle1) > 1.0e-4f);
		}

		const juce::ColourGradient& buildCached(float _alpha) const;

		const juce::Image& rasteriseCached(const juce::Rectangle<int>& _deviceArea,
			const juce::AffineTransform& _userToDevice, float _alpha) const;

	private:
		juce::ColourGradient build(float _alpha) const;

		juce::Image rasterise(const juce::Rectangle<int>& _deviceArea,
			const juce::AffineTransform& _userToDevice, float _alpha) const;

		juce::Colour colourAt(double _t, float _alpha) const;

		mutable juce::ColourGradient m_builtGradient;
		mutable int m_builtVersion = -1;
		mutable float m_builtAlpha = 0.0f;

		mutable juce::Image m_raster;
		mutable int m_rasterVersion = -1;
		mutable float m_rasterAlpha = 0.0f;
		mutable juce::Rectangle<int> m_rasterArea;
		mutable juce::AffineTransform m_rasterTransform;
	};

	// html5 2d context over juce::Graphics and its image, live only in a paint; drawing outside no-ops
	class Context2D
	{
	public:
		void begin(juce::Image& _image, juce::Graphics& _g, Rml::CoreInstance& _coreInstance, ElemCanvas& _element)
		{
			m_image = &_image;
			m_graphics = &_g;
			m_coreInstance = &_coreInstance;
			m_element = &_element;
			m_graphics->saveState();	// the juce::Graphics outlives one paint, so a clip must not leak into the next
		}
		void end()
		{
			if (m_graphics)
			{
				// unwind anything the script saved and never restored, then the paint's own bracket
				while (!m_savedStates.empty())
					restore();
				m_graphics->restoreState();
			}
			m_graphics = nullptr;
			m_image = nullptr;
			m_coreInstance = nullptr;
			m_element = nullptr;
			m_path.clear();
			m_pathStarted = false;
			state = DrawingState();
		}

		juce::Graphics* graphics() const { return m_graphics; }
		juce::Image* image() const { return m_image; }
		Rml::CoreInstance* coreInstance() const { return m_coreInstance; }
		ElemCanvas* element() const { return m_element; }	// what html5 reaches through ctx.canvas

		juce::Path& path() { return m_path; }
		const juce::Path& path() const { return m_path; }
		bool& pathStarted() { return m_pathStarted; }

		// exactly what html5 save()/restore() carries; the current path is deliberately not, per spec
		struct DrawingState
		{
			juce::Colour fillColour{ juce::Colours::black };
			juce::Colour strokeColour{ juce::Colours::black };
			float lineWidth = 1.0f;
			juce::PathStrokeType::EndCapStyle lineCap = juce::PathStrokeType::butt;		// html5 defaults
			juce::PathStrokeType::JointStyle lineJoin = juce::PathStrokeType::mitered;
			float globalAlpha = 1.0f;
			std::vector<float> lineDash;	// alternating on/off lengths, empty means a solid line
			float lineDashOffset = 0.0f;
			juce::AffineTransform transform;
			std::shared_ptr<CanvasGradient> fillGradient;	// null means the plain colour is used
			std::shared_ptr<CanvasGradient> strokeGradient;
			juce::Colour shadowColour{ juce::Colours::transparentBlack };
			float shadowBlur = 0.0f;
			float shadowOffsetX = 0.0f;
			float shadowOffsetY = 0.0f;
		};

		DrawingState state;

		void save()
		{
			if (!m_graphics)
				return;
			m_savedStates.push_back(state);
			m_graphics->saveState();
		}

		void restore()
		{
			if (!m_graphics || m_savedStates.empty())	// html5 ignores a restore with an empty stack
				return;
			state = m_savedStates.back();
			m_savedStates.pop_back();
			m_graphics->restoreState();
		}

		// html5 maps a point through the current transform as it is added to the path, not at draw time
		juce::Point<float> transformed(const juce::Point<float> _p) const { return _p.transformedBy(state.transform); }
		juce::Point<float> transformed(const float _x, const float _y) const { return transformed(juce::Point<float>(_x, _y)); }

		juce::Path buildStrokeOutline() const;
		juce::Path strokeOutlineOf(const juce::Path& _userPath) const;

		// html5 needs a visible colour plus a blur or offset; the offsets are deliberately not transformed
		bool hasShadow() const
		{
			return state.shadowColour.getAlpha() != 0 &&
				(state.shadowBlur > 0.0f || state.shadowOffsetX != 0.0f || state.shadowOffsetY != 0.0f);
		}

		void drawShadow(juce::Graphics& _g, const juce::Path& _path) const;
		void warnUnsupported(const char* _name);

		// _deviceBounds is what will be painted, so a rasterised gradient covers that, not the whole clip
		void applyFill(juce::Graphics& _g, juce::Rectangle<int> _deviceBounds) const
		{
			apply(_g, state.fillGradient, fill(), _deviceBounds);
		}

		void applyStroke(juce::Graphics& _g, juce::Rectangle<int> _deviceBounds) const
		{
			apply(_g, state.strokeGradient, stroke(), _deviceBounds);
		}

	private:
		void apply(juce::Graphics& _g, const std::shared_ptr<CanvasGradient>& _gradient, juce::Colour _colour,
			juce::Rectangle<int> _deviceBounds) const;

		float transformScale() const { return std::sqrt(std::abs(state.transform.getDeterminant())); }
		std::vector<float> dashPattern() const;

		// html5 multiplies globalAlpha with the source alpha rather than replacing it
		juce::Colour fill() const { return state.fillColour.withMultipliedAlpha(state.globalAlpha); }
		juce::Colour stroke() const { return state.strokeColour.withMultipliedAlpha(state.globalAlpha); }

		juce::Graphics* m_graphics = nullptr;
		juce::Image* m_image = nullptr;
		Rml::CoreInstance* m_coreInstance = nullptr;
		ElemCanvas* m_element = nullptr;
		juce::Path m_path;
		bool m_pathStarted = false;
		std::vector<DrawingState> m_savedStates;
		std::set<const void*> m_warnedUnsupported;	// literal addresses, so no string is built to look one up
	};

	// Registers the "Canvas" (canvas element) and "CanvasRenderingContext2D"
	// Lua types. Call once, after Rml::Lua::Initialise.
	void registerCanvasLua(lua_State* _L);

}
