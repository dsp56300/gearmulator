#pragma once
#include "jucePluginEditorLib/pluginProcessor.h"
#include "nmmDevice.h"

namespace nmmJucePlugin
{
    class EditorMidi;
    class Processor final : public jucePluginEditorLib::Processor
    {
    public:
        Processor();
        ~Processor() override;
        jucePluginEditorLib::PluginEditorState* createEditorState() override;
        synthLib::Device* createDevice() override;
        pluginLib::Controller* createController() override;
        void setNonRealtime(bool offline) noexcept override;
        bool isBusesLayoutSupported(const BusesLayout& layout) const override;
        std::shared_ptr<PanelState> panel() const { return m_panel; }
        bool editorMidiEnabled() const {return bool(m_editorMidi);}
        void setEditorMidiEnabled(bool enabled);
    private:
        std::shared_ptr<PanelState> m_panel=std::make_shared<PanelState>();
        std::unique_ptr<EditorMidi> m_editorMidi;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
    };
}
