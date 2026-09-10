#include "nmmPluginProcessor.h"
#include "nmmEditor.h"
#include "nmmEditorMidi.h"
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"
#include "jucePluginLib/controller.h"
#include "synthLib/romLoader.h"
#include "synthLib/deviceException.h"
#include "nmmLib/nmmrom.h"

namespace nmmJucePlugin
{
    namespace
    {
        juce::PropertiesFile::Options options()
        {
            juce::PropertiesFile::Options o;
            o.applicationName="DSP56300EmulatorNordMicroModular";
            o.filenameSuffix=".settings"; o.folderName=o.applicationName;
            o.osxLibrarySubFolder="Application Support/"+o.applicationName;
            return o;
        }
        class Controller final : public pluginLib::Controller
        {
        public:
            explicit Controller(Processor& p):pluginLib::Controller(p,"parameterDescriptions_nmm.json"),m_panel(p.panel()) {registerParams(p);}
            uint8_t getPartCount() const override {return 1;}
            void sendParameterChange(const pluginLib::Parameter& p,pluginLib::ParamValue v,pluginLib::Parameter::Origin) override
            {
                const auto index=p.getDescription().index;
                if(index<4) m_panel->setValue(index,std::clamp<int>(v,0,127));
            }
            bool parseSysexMessage(const pluginLib::SysEx&,synthLib::MidiEventSource) override {return false;}
            void onStateLoaded() override
            {
                const char* names[]{"MasterVolume","Knob1","Knob2","Knob3"};
                for(unsigned i=0;i<4;++i) if(auto* p=getParameter(names[i],0))
                    p->setUnnormalizedValue(m_panel->values[i],pluginLib::Parameter::Origin::PresetChange);
            }
        private:
            std::shared_ptr<PanelState> m_panel;
        };
    }
    Processor::Processor():jucePluginEditorLib::Processor(BusesProperties().withInput("Audio In",juce::AudioChannelSet::stereo(),false).withOutput("Out 1 / 2",juce::AudioChannelSet::stereo(),true),options(),pluginLib::initProcessorProperties())
    {
        for(const char* filename:{"101.pch","SimpleSqr1.pch","BasicOsc.pch","FourVoices.pch"})
          for(int i=0;i<BinaryData::namedResourceListSize;++i)
            if(std::string(BinaryData::originalFilenames[i])==filename)
            {
                int size=0;const auto* data=BinaryData::getNamedResource(BinaryData::namedResourceList[i],size);
                m_panel->bank.push_back({juce::String(filename).upToLastOccurrenceOf(".",false,false).toStdString(),std::string(data,static_cast<size_t>(size))});
            }
        m_panel->patchText=m_panel->bank.front().text;m_panel->patchName=m_panel->bank.front().name;
        m_autosave=std::make_unique<Autosave>(m_panel,juce::File(getConfigFolder()));
        getController();
        if(getConfig().getBoolValue("nmmEditorMidi",true)) m_editorMidi=std::make_unique<EditorMidi>(m_panel->editor);
        else m_panel->editor->ports="Editor MIDI disabled";
        setLatencyBlocks(getConfig().getIntValue("latencyBlocks",static_cast<int>(getPlugin().getLatencyBlocks())));
    }
    Processor::~Processor() {destroyEditorState();m_editorMidi.reset();m_autosave.reset();}
    void Processor::setEditorMidiEnabled(bool enabled)
    {
        if(enabled && !m_editorMidi) m_editorMidi=std::make_unique<EditorMidi>(m_panel->editor);
        if(!enabled)
        {
            m_editorMidi.reset();
            std::lock_guard<std::mutex> lock(m_panel->editor->mutex);m_panel->editor->ports="Editor MIDI disabled";
        }
        getConfig().setValue("nmmEditorMidi",enabled);
    }
    bool Processor::isBusesLayoutSupported(const BusesLayout& layout) const
    {
        return (layout.inputBuses.isEmpty() || (layout.inputBuses.size()==1 && (layout.getMainInputChannelSet().isDisabled() || layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo()))) && layout.outputBuses.size()==1
            && layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    pluginLib::Controller* Processor::createController() {return new Controller(*this);}
    jucePluginEditorLib::PluginEditorState* Processor::createEditorState() {return new EditorState(*this);}
    synthLib::Device* Processor::createDevice()
    {
        for(const auto& path:synthLib::RomLoader::findFiles(".bin",354016,354016))
        {
            try {nmm::Rom rom(path);} catch(const std::exception&) {continue;}
            return new Device({},path,m_panel);
        }
        {std::lock_guard<std::mutex> lock(m_panel->mutex);m_panel->status="Firmware missing";m_panel->loading=false;m_panel->failed=true;}
        throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,"Nord Micro Modular requires the decoded OS 3.03b firmware (354016 bytes).");
    }
    void Processor::setNonRealtime(bool offline) noexcept
    {
        juce::AudioProcessor::setNonRealtime(offline);
        m_panel->offline=offline;
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {return new nmmJucePlugin::Processor();}
