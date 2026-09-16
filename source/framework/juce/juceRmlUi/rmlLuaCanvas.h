#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "juce_graphics/juce_graphics.h"

struct lua_State;

namespace juceRmlUi
{
	class ElemCanvas;

	// HTML5 CanvasGradient. Lua owns this object, while a fill or stroke style shares its data, so colour
	// stops added after the gradient was assigned still apply, as they do in HTML5.
	struct CanvasGradient
	{
		struct Data
		{
			bool radial = false;
			juce::Point<float> start, end;	// linear: the gradient line, radial: the centres of the two circles
			float startRadius = 0.0f;
			float endRadius = 0.0f;
			std::vector<std::pair<float, juce::Colour>> stops;	// by offset, equal offsets in the order they were added
		};

		std::shared_ptr<Data> data;
	};

	// HTML5 fillStyle / strokeStyle: a colour, or a gradient if one is set
	struct CanvasStyle
	{
		juce::Colour colour{ juce::Colours::black };
		std::shared_ptr<CanvasGradient::Data> gradient;
	};

	// Minimal HTML5-canvas-style 2D drawing context. It wraps a juce::Graphics
	// that is only valid while a canvas paint callback is running; outside of
	// that window graphics() is null and drawing methods are no-ops.
	class Context2D
	{
	public:
		explicit Context2D(ElemCanvas* _canvas) : m_canvas(_canvas) {}

		void begin(juce::Graphics& _g)
		{
			m_graphics = &_g;
			// the graphics outlives the paint, a clip() must not
			_g.saveState();
		}
		void end()
		{
			m_graphics->restoreState();
			m_graphics = nullptr;
			m_path.clear();
			m_pathStarted = false;
		}

		juce::Graphics* graphics() const { return m_graphics; }
		ElemCanvas* canvas() const { return m_canvas; }

		juce::Path& path() { return m_path; }
		bool& pathStarted() { return m_pathStarted; }

		// HTML5 context state
		CanvasStyle fillStyle;
		CanvasStyle strokeStyle;
		float lineWidth = 1.0f;
		juce::PathStrokeType::JointStyle lineJoin = juce::PathStrokeType::mitered;
		juce::PathStrokeType::EndCapStyle lineCap = juce::PathStrokeType::butt;

	private:
		ElemCanvas* m_canvas;
		juce::Graphics* m_graphics = nullptr;
		juce::Path m_path;
		bool m_pathStarted = false;
	};

	// Registers the "Canvas" (canvas element), "CanvasContext" and "CanvasGradient"
	// Lua types. Call once, after Rml::Lua::Initialise.
	void registerCanvasLua(lua_State* _L);
}
