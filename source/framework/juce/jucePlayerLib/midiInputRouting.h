#pragma once

#include "juce_audio_devices/juce_audio_devices.h"
#include "juce_data_structures/juce_data_structures.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include "synthLib/midiTypes.h"

namespace jucePlayer
{
    // Routes each MIDI input to selected destination groups; zero groups closes the input.
    class MidiInputRouting final : private juce::MidiInputCallback
    {
    public:
        static constexpr uint8_t GroupA = 1;
        // Called from the MIDI thread; bit n of the groups is part group n.
        using Deliver = std::function<void(const juce::MidiMessage&, uint8_t)>;

        MidiInputRouting(juce::AudioDeviceManager& _manager, juce::PropertiesFile& _config, Deliver _deliver,
                         uint8_t _groupCount);
        ~MidiInputRouting() override;

        // The groups an input plays; zero while it is closed.
        uint8_t groups(const juce::String& _identifier) const;
        // Opens or closes the input to match, and remembers the choice.
        void setGroups(const juce::MidiDeviceInfo& _input, uint8_t _groups);

    private:
        struct Route
        {
            juce::String name;
            uint8_t groups = 0;
        };

        void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message) override;
        void persist();

        uint8_t m_allGroups;
        juce::AudioDeviceManager& m_manager;
        juce::PropertiesFile& m_config;
        Deliver m_deliver;
        mutable std::mutex m_mutex;
        std::map<juce::String, Route> m_routes; // by device identifier
    };

    // Preserve source identity when converting timestamped JUCE input to engine events.
    template <typename Deliver> void forEachMidiEvent(const juce::MidiBuffer& _midi, uint8_t _port,
                                                      synthLib::MidiEventSource _source, Deliver&& deliver)
    {
        for (const auto metadata : _midi)
        {
            const auto message = metadata.getMessage();
            synthLib::SMidiEvent event(_source);
            event.offset = static_cast<uint32_t>(std::max(0, metadata.samplePosition));
            event.port = _port;
            if (message.isSysEx())
            {
                event.sysex.push_back(0xf0);
                const auto* data = message.getSysExData();
                for (int i = 0; i < message.getSysExDataSize(); ++i)
                    event.sysex.push_back(data[i]);
                event.sysex.push_back(0xf7);
            }
            else
            {
                const int size = message.getRawDataSize();
                if (size == 0 || size > 3)
                    continue;
                synthLib::setShortMessage(event, message.getRawData(), static_cast<size_t>(size));
            }
            deliver(std::move(event));
        }
    }

} // namespace jucePlayer
