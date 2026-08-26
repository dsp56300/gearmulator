#pragma once

#include "juce_graphics/juce_graphics.h"

struct lua_State;

namespace juceRmlUi
{
	// Minimal HTML5-canvas-style 2D drawing context. It wraps a juce::Graphics
	// that is only valid while a canvas paint callback is running; outside of
	// that window graphics() is null and drawing methods are no-ops.
	class Context2D
	{
	public:
		void begin(juce::Graphics& _g) { m_graphics = &_g; }
		void end()
		{
			m_graphics = nullptr;
			m_path.clear();
			m_pathStarted = false;
		}

		juce::Graphics* graphics() const { return m_graphics; }

		juce::Path& path() { return m_path; }
		bool& pathStarted() { return m_pathStarted; }

		// HTML5 context state
		juce::Colour fillColour{ juce::Colours::black };
		juce::Colour strokeColour{ juce::Colours::black };
		float lineWidth = 1.0f;

	private:
		juce::Graphics* m_graphics = nullptr;
		juce::Path m_path;
		bool m_pathStarted = false;
	};

	// Registers the "Canvas" (canvas element) and "CanvasRenderingContext2D"
	// Lua types. Call once, after Rml::Lua::Initialise.
	void registerCanvasLua(lua_State* _L);
}
