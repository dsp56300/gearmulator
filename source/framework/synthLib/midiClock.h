#pragma once

#include <cstddef>
#include <cstdint>

namespace synthLib
{
	class Plugin;

	class MidiClock
	{
	public:
		explicit MidiClock(Plugin& _plugin) : m_plugin(_plugin) {}

		void process(float _bpm, double _ppqPos, bool _isPlaying, size_t _sampleCount);

		void restart();

	private:
		void stop();
		void start(double _ppqPos);

		Plugin& m_plugin;

		bool m_isPlaying = false;
		double m_clockTickPos = 0.0;
	};
}
