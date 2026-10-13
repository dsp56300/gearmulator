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
        // With how far ahead of now the sender timestamped the event, in milliseconds: CoreMIDI senders may schedule
        // ahead, and a receiver that passes the event on keeps their timing by waiting that long. 0 when it is due now.
        using TimedMidiInputCallback = std::function<void(synthLib::SMidiEvent, double _leadMs)>;

        explicit PortMidiBridge(MidiInputCallback _midiInputCallback, std::string _name, uint8_t _portCount);
        // _withOutputs false creates the "MIDI IN" ports only
        PortMidiBridge(TimedMidiInputCallback _midiInputCallback, std::string _name, uint8_t _portCount, bool _withOutputs);
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

        TimedMidiInputCallback m_midiInputCallback;
        const std::string m_name;
        const uint8_t m_portCount;
        const bool m_withOutputs;
        std::atomic<bool> m_enabled{false};
        dsp56k::RingBuffer<synthLib::SMidiEvent, 4096, false> m_output;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PortMidiBridge)
    };
} // namespace jucePlayer
