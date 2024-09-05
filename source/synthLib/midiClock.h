#pragma once

#include <cstddef>

namespace synthLib
{
	class Plugin;

	class MidiClock
	{
	public:
		MidiClock(Plugin& _plugin) : m_plugin(_plugin)
		{
		}

		void process(float _bpm, float _ppqPos, bool _isPlaying, size_t _sampleCount);
		void restart();

	private:
		Plugin& m_plugin;

		bool m_isPlaying = false;
		bool m_needsStart = false;
		double m_clockTickPos = 0.0;
	};
}