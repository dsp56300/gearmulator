#pragma once

#include "synthLib/midiTypes.h"

#include "dsp56kBase/ringbuffer.h"

#include "juce_core/juce_core.h"

#include <array>
#include <atomic>
#include <functional>

namespace emu88Player
{
	class PortMidiBridge final : private juce::Thread
	{
	public:
		using MidiInputCallback = std::function<void(synthLib::SMidiEvent)>;

		explicit PortMidiBridge(MidiInputCallback _midiInputCallback);
		~PortMidiBridge() override;

		static bool isOwnVirtualPortName(const juce::String& _name);

		// PortMidi implements Pm_CreateVirtualInput/Output for CoreMIDI and
		// ALSA only - its Windows MM backend registers no interface for them at
		// all, so the call returns pmNotImplemented and no endpoint is ever
		// created. The bridge has nothing to open there, so it is not started
		// and the settings page says so instead of offering a dead switch.
		static constexpr bool virtualPortsSupported()
		{
#if JUCE_WINDOWS
			return false;
#else
			return true;
#endif
		}

		void setEnabled(bool _enabled);
		bool isEnabled() const { return m_enabled.load(std::memory_order_acquire); }
		void enqueueOutput(const synthLib::SMidiEvent& _event);

	private:
		void run() override;

		MidiInputCallback m_midiInputCallback;
		std::atomic<bool> m_enabled{false};
		dsp56k::RingBuffer<synthLib::SMidiEvent, 4096, false> m_output;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PortMidiBridge)
	};
}
