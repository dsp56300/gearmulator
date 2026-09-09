#include "nmmEditorMidi.h"

namespace nmmJucePlugin
{
    EditorMidi::EditorMidi(std::shared_ptr<EditorTransport> transport)
        : juce::Thread("Nord editor MIDI"),m_transport(std::move(transport))
    {
#if JUCE_MAC || JUCE_LINUX
        const auto name="Nord Micro Modular "+juce::Uuid().toString().substring(0,8);
        m_output=juce::MidiOutput::createNewDevice(name+" PC Out");
        m_input=juce::MidiInput::createNewDevice(name+" PC In",this);
        if(m_input && m_output)
        {
            {std::lock_guard<std::mutex> lock(m_transport->mutex);m_transport->ports=name.toStdString();}
            startThread();m_input->start();
        }
        else {std::lock_guard<std::mutex> lock(m_transport->mutex);m_transport->ports="Editor MIDI ports unavailable";}
#else
        std::lock_guard<std::mutex> lock(m_transport->mutex);
        m_transport->ports="Virtual editor MIDI ports require macOS or Linux";
#endif
    }
    EditorMidi::~EditorMidi()
    {
        if(m_input) m_input->stop();
        stopThread(-1);
    }
    void EditorMidi::handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage& message)
    {
        m_transport->receive(message.getRawData(),static_cast<size_t>(message.getRawDataSize()));
    }
    void EditorMidi::run()
    {
        std::vector<uint8_t> bytes,message;
        message.reserve(512);
        while(!threadShouldExit())
        {
            {std::lock_guard<std::mutex> lock(m_transport->mutex);bytes.swap(m_transport->output);}
            for(auto b:bytes)
            {
                // Real-time bytes can occur inside SysEx and must not split it.
                if(b>=0xf8) {sendOutput(juce::MidiMessage(&b,1));continue;}
                if(b==0xf0) {message.clear();message.push_back(b);continue;}
                if(message.empty()) continue;
                if((b&0x80) && b!=0xf7) {message.clear();continue;}
                if(message.size()==EditorTransport::Capacity-1) {message.clear();continue;}
                message.push_back(b);
                if(b==0xf7) {sendOutput(juce::MidiMessage(message.data(),static_cast<int>(message.size())));message.clear();}
            }
            bytes.clear();wait(1);
        }
    }
    void EditorMidi::sendOutput(const juce::MidiMessage& message)
    {
        if(m_output) m_output->sendMessageNow(message);
    }
}
