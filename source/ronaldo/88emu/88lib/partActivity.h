#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace emu88Lib
{
	// Which of a board's parts the channel messages reach, for an editor to light up the parts that play: a bit per
	// part, set without a lock from a table of the parts that listen on each port and channel. Kept only while
	// enabled, for as long as something shows it; otherwise a channel message pays a single load for it.
	//
	// process() runs on whatever thread hands the board its MIDI, take() on the one that shows it. The table comes
	// from whoever knows the parts' channels; a message that meets an outdated table lights the parts that listened
	// before, which a light can live with.
	class PartActivity
	{
	public:
		static constexpr size_t g_maxPorts = 4;
		using Listeners = std::array<uint64_t, g_maxPorts * 16>;

		void setEnabled(bool _enabled);
		bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }

		// The parts that listen on each port and channel, at port * 16 + channel, a bit per part
		void setListeners(const Listeners& _listeners);

		// A message to the board on a port
		void process(uint8_t _status, uint8_t _port)
		{
			if(!m_enabled.load(std::memory_order_relaxed))
				return;
			if(_status < 0x80 || _status >= 0xf0 || _port >= g_maxPorts)
				return;
			const auto parts = m_listeners[_port * 16u + (_status & 0x0f)].load(std::memory_order_relaxed);
			// Seen already: most notes leave it at a load
			if(parts && (m_activity.load(std::memory_order_relaxed) & parts) != parts)
				m_activity.fetch_or(parts, std::memory_order_relaxed);
		}

		// The parts reached since the last call
		uint64_t take() { return m_activity.exchange(0, std::memory_order_relaxed); }

	private:
		std::array<std::atomic<uint64_t>, g_maxPorts * 16> m_listeners{};
		std::atomic<uint64_t> m_activity{0};
		std::atomic<bool> m_enabled{false};
	};
}
