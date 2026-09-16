#pragma once

#include "synthLib/midiTypes.h"

#include "dsp56kBase/ringbuffer.h"

#include "juce_core/juce_core.h"

#include <array>
#include <atomic>
#include <functional>

namespace jucePlayer
{
    class PortMidiBridge final : private juce::Thread
    {
    public:
        using MidiInputCallback = std::function<void(synthLib::SMidiEvent)>;

        explicit PortMidiBridge(MidiInputCallback _midiInputCallback, std::string _name, uint8_t _portCount);
        ~PortMidiBridge() override;

        static bool isOwnVirtualPortName(const juce::String& _name);

        // PortMidi's CoreMIDI and ALSA backends support virtual ports; Windows MM does not.
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
        const std::string m_name;
        const uint8_t m_portCount;
        std::atomic<bool> m_enabled{false};
        dsp56k::RingBuffer<synthLib::SMidiEvent, 4096, false> m_output;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PortMidiBridge)
    };
} // namespace jucePlayer
