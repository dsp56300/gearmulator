#pragma once
#include "nmmEditorTransport.h"
#include "juce_audio_devices/juce_audio_devices.h"

namespace nmmJucePlugin
{
    class EditorMidi final : private juce::MidiInputCallback, private juce::Thread
    {
    public:
        explicit EditorMidi(std::shared_ptr<EditorTransport> transport);
        ~EditorMidi() override;
    private:
        void handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage&) override;
        void run() override;
        void sendOutput(const juce::MidiMessage&);
        std::shared_ptr<EditorTransport> m_transport;
        std::unique_ptr<juce::MidiInput> m_input;
        std::unique_ptr<juce::MidiOutput> m_output;
    };
}
