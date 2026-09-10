#include "playback.h"
#include "routing.h"
#include "dsp56kBase/logging.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <csignal>
#include <ctime>
#include <iostream>
#include <sstream>

namespace
{
volatile std::sig_atomic_t interrupted=0;
void interrupt(int) {interrupted=1;}
double number(const std::string& s)
{
    size_t end=0;const double n=std::stod(s,&end);
    if(end!=s.size()||!std::isfinite(n)) throw std::invalid_argument("Invalid number: "+s);
    return n;
}
struct Callback final:juce::AudioIODeviceCallback
{
    nmrack::Playback& playback;
    const double rate;
    const int block;
    const nmrack::Monitor monitor;
    const std::vector<int> map;
    const float gain;
    std::atomic<int> fault{0}; // 1=format, 2=stopped, 3=backend error, 4=channels
    std::atomic<uint64_t> frames{0};
    Callback(nmrack::Playback& p,double r,int b,std::string m,std::vector<int> destinations,float g)
        :playback(p),rate(r),block(b),monitor(m=="mix"?nmrack::Monitor::Mix:m=="34"?nmrack::Monitor::Pair34:
            m=="four"?nmrack::Monitor::Four:nmrack::Monitor::Pair12),map(std::move(destinations)),gain(g) {}
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override
    {
        if(d->getCurrentSampleRate()!=rate||d->getCurrentBufferSizeSamples()!=block)
            fault.store(1,std::memory_order_relaxed);
    }
    void audioDeviceStopped() override {fault.store(2,std::memory_order_relaxed);}
    void audioDeviceError(const juce::String&) override {fault.store(3,std::memory_order_relaxed);}
    void audioDeviceIOCallbackWithContext(const float* const*,int,float* const* out,int channels,int samples,
                                         const juce::AudioIODeviceCallbackContext&) override
    {
        for(int ch=0;ch<channels;++ch) if(out[ch]) std::fill_n(out[ch],samples,0.0f);
        if(fault.load(std::memory_order_relaxed)) return;
        for(const auto ch:map) if(ch>=channels||!out[ch]) {fault.store(4,std::memory_order_relaxed);return;}
        std::array<nmrack::AudioFrame,128> buffer;
        for(int offset=0;offset<samples;)
        {
            const auto count=unsigned(std::min(samples-offset,128));
            playback.consume(buffer.data(),count);
            for(unsigned i=0;i<count;++i) for(unsigned ch=0;ch<map.size();++ch)
            {
                out[map[ch]][offset+int(i)]=nmrack::monitorSample(buffer[i],ch,monitor,gain);
            }
            offset+=int(count);
        }
        frames.fetch_add(uint64_t(samples),std::memory_order_relaxed);
    }
};
// Declared after callback: stop/join the device before callback or worker destruction.
struct StopDevice {juce::AudioIODevice* device;~StopDevice(){if(device){device->stop();device->close();}}};
struct MidiBridge final:juce::MidiInputCallback
{
    nmrack::Playback& playback;
    explicit MidiBridge(nmrack::Playback& p):playback(p) {}
    void handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage& message) override
    {
        const auto size=message.getRawDataSize();const auto* data=message.getRawData();
        if(size<2||size>3||data[0]<0x80||data[0]>=0xf0) return; // no editor SysEx on performance MIDI
        try {playback.submit({nmrack::Command::Midi,playback.nativeFrame(),data[0],data[1],uint16_t(size==3?data[2]:0),0});}
        catch(...) {playback.allNotesOff();}
    }
};
struct StopMidi {juce::MidiInput* input;~StopMidi(){if(input) input->stop();}};
}
int main(int argc,char** argv)
{
    try
    {
        std::string rom,deviceName,backend,monitor="12",mapping,patchPath,midiName;
        double rate=48000,seconds=0,gain=0.2;int block=256;unsigned requestedVoices=0;
        bool list=false,simulate=false,tones=false,listMidi=false,demo=false;
        for(int i=1;i<argc;++i)
        {
            const std::string arg=argv[i];
            auto value=[&]()->std::string {if(i+1==argc) throw std::invalid_argument("Missing value for "+arg);return argv[++i];};
            if(arg=="--help")
            {
                std::cout<<"nmrackPlayer ROM [--device NAME] [--backend NAME] [--rate 48000] [--buffer 256]\n"
                    <<"  [--monitor 12|34|mix|four] [--map 1,2[,3,4]] [--gain 0.2]\n"
                    <<"  [--output-tones] [--seconds N] [--simulate]\n"
                    <<"  [--patch FILE.pch] [--voices 1..32] [--midi-input NAME] [--midi-demo]\n"
                    <<"nmrackPlayer --list-midi\n"
                    <<"nmrackPlayer --list-devices\nDefault: native 330-Hz test patch; Ctrl-C stops playback.\n";return 0;
            }
            else if(arg=="--list-devices") list=true;
            else if(arg=="--simulate") simulate=true;
            else if(arg=="--output-tones") tones=true;
            else if(arg=="--patch") patchPath=value();
            else if(arg=="--midi-input") midiName=value();
            else if(arg=="--list-midi") listMidi=true;
            else if(arg=="--midi-demo") demo=true;
            else if(arg=="--voices") {const auto n=number(value());if(n<1||n>32||n!=std::floor(n)) throw std::invalid_argument("Voices must be 1..32");requestedVoices=unsigned(n);}
            else if(arg=="--device") deviceName=value();
            else if(arg=="--backend") backend=value();
            else if(arg=="--monitor") monitor=value();
            else if(arg=="--map") mapping=value();
            else if(arg=="--rate") rate=number(value());
            else if(arg=="--seconds") seconds=number(value());
            else if(arg=="--gain") gain=number(value());
            else if(arg=="--buffer") {const auto n=number(value());if(n<16||n>2048||n!=std::floor(n)) throw std::invalid_argument("Buffer must be 16..2048 whole frames");block=int(n);}
            else if(!arg.empty()&&arg[0]!='-'&&rom.empty()) rom=arg;
            else throw std::invalid_argument("Unknown option: "+arg);
        }
        if(rate<32000||rate>192000||gain<0||gain>1||seconds<0||seconds>86400) throw std::invalid_argument("Invalid rate, gain or duration");
        if(!list&&!listMidi&&rom.empty()) throw std::invalid_argument("ROM required; use --help");
        if((!midiName.empty()||demo||requestedVoices)&&patchPath.empty()) throw std::invalid_argument("MIDI playback/voice override requires --patch");
        if(tones&&!patchPath.empty()) throw std::invalid_argument("Choose --patch or --output-tones");
        std::shared_ptr<const nmm::Patch> patch;
        if(!patchPath.empty())
        {
            auto parsed=nmm::Patch::load(patchPath);if(requestedVoices) parsed.requestedVoices=uint8_t(requestedVoices);
            patch=std::make_shared<const nmm::Patch>(std::move(parsed));
        }
        if(monitor!="12"&&monitor!="34"&&monitor!="mix"&&monitor!="four") throw std::invalid_argument("Invalid monitor mode");
        std::vector<int> map;const unsigned destinations=monitor=="four"?4:2;
        if(mapping.empty()) for(unsigned i=0;i<destinations;++i) map.push_back(int(i));
        else
        {
            std::istringstream stream(mapping);std::string item;
            while(std::getline(stream,item,','))
            {const auto n=number(item);if(n<1||n>256||n!=std::floor(n)) throw std::invalid_argument("Invalid output channel");map.push_back(int(n)-1);}
            if(mapping.back()==',') throw std::invalid_argument("Invalid channel map");
        }
        if(map.size()!=destinations) throw std::invalid_argument("Channel map does not match monitor mode");
        auto sorted=map;std::sort(sorted.begin(),sorted.end());
        if(std::adjacent_find(sorted.begin(),sorted.end())!=sorted.end()) throw std::invalid_argument("Output destinations must be unique");
        const int channels=sorted.back()+1;
        juce::ScopedJuceInitialiser_GUI juce;
        if(listMidi)
        {
            for(const auto& info:juce::MidiInput::getAvailableDevices()) std::cout<<info.name<<'\n';
            return 0;
        }
        juce::AudioDeviceManager manager;
        std::unique_ptr<juce::AudioIODevice> device;
        if(list||!simulate)
        {
            juce::AudioIODeviceType* selected=nullptr;juce::String selectedName;
            for(auto* type:manager.getAvailableDeviceTypes())
            {
                type->scanForDevices();const auto names=type->getDeviceNames(false);
                for(const auto& name:names) if(list) std::cout<<type->getTypeName()<<": "<<name<<'\n';
                if(list||(!backend.empty()&&type->getTypeName()!=juce::String(backend))||names.isEmpty()) continue;
                const auto index=deviceName.empty()?type->getDefaultDeviceIndex(false):names.indexOf(juce::String(deviceName));
                if(index<0||index>=names.size()) continue;
                if(selected&&!deviceName.empty()) throw std::runtime_error("Ambiguous device name; specify --backend");
                if(!selected) {selected=type;selectedName=names[index];}
            }
            if(list) return 0;
            if(!selected) throw std::runtime_error("No matching audio output device; use --list-devices or --simulate");
            device.reset(selected->createDevice(selectedName,{}));
            if(!device||device->getOutputChannelNames().size()<channels) throw std::runtime_error("Device has insufficient output channels");
            juce::BigInteger outputs;outputs.setRange(0,channels,true);
            const auto error=device->open({},outputs,rate,block);
            if(error.isNotEmpty()) throw std::runtime_error(error.toStdString());
            // Drain asynchronous format-change notifications before attaching a callback.
            juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
            rate=device->getCurrentSampleRate();block=device->getCurrentBufferSizeSamples();
            std::cout<<"device="<<selectedName<<" output_latency_frames="<<device->getOutputLatencyInSamples()<<'\n';
        }
        if(rate<32000||rate>192000||block<1||block>2048) throw std::runtime_error("Unsupported negotiated audio format");
        // Four blocks absorb isolated host stalls, not sustained emulation overload.
        const unsigned target=std::max(512u,((unsigned(block)*4+127)/128)*128);
        Logging::setLogFunc([](const std::string&){});
        nmrack::Hardware::Options options;options.wordSerial=true;options.experimentalChain=true;
        auto machine=std::make_unique<nmrack::Hardware>(rom,options);
        machine->setStepBudget(450000000);machine->boot();
        if(!patch) {if(tones) machine->initializeOutputTones();else machine->initializeTone();machine->advance(96000);}
        if(device) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        nmrack::Playback playback(std::move(machine),rate,target,patch);playback.prefill(std::chrono::seconds(30));
        MidiBridge midiBridge(playback);std::unique_ptr<juce::MidiInput> midi;
        if(!midiName.empty())
        {
            for(const auto& info:juce::MidiInput::getAvailableDevices()) if(info.name==juce::String(midiName))
            {
                if(midi) throw std::runtime_error("Ambiguous MIDI input name");
                midi=juce::MidiInput::openDevice(info.identifier,&midiBridge);
                if(!midi) throw std::runtime_error("Cannot open MIDI input");
            }
            if(!midi) throw std::runtime_error("MIDI input not found; use --list-midi");
        }
        StopMidi stopMidi{midi.get()};
        Callback callback(playback,rate,block,monitor,map,float(gain));StopDevice stop{device.get()};
        std::signal(SIGINT,interrupt);std::signal(SIGTERM,interrupt);
        std::cout<<"mode="<<(simulate?"simulated":"device")<<" rate="<<rate<<" buffer="<<block
            <<" queue_target_ms="<<target*1000.0/rate<<" gain="<<gain
            <<" allocated_voices="<<playback.allocatedVoices()<<" voice_dsp_mask="<<playback.voiceDspMask()<<std::endl;
        std::vector<std::vector<float>> scratch(channels,std::vector<float>(block));std::vector<float*> pointers;
        for(auto& channel:scratch) pointers.push_back(channel.data());
        const auto start=std::chrono::steady_clock::now();const auto cpuStart=std::clock();
        if(device) device->start(&callback);
        if(midi) midi->start();
        uint64_t simulatedFrames=0,lastFrames=0,nextDemo=playback.nativeFrame()+9600;auto heartbeat=start;
        unsigned demoChord=0;
        while(!interrupted)
        {
            playback.checkError();
            if(demo&&playback.nativeFrame()+9600>=nextDemo)
            {
                const auto notes=requestedVoices?requestedVoices:4u;
                for(unsigned i=0;i<notes;++i)
                {
                    const auto note=uint16_t((requestedVoices?48+i:std::array<unsigned,4>{60,64,67,72}[i])+unsigned(demoChord%2)*2);
                    if(!playback.submit({nmrack::Command::Midi,nextDemo,0x90,note,uint16_t(60+(demoChord%3)*20),0})||
                       !playback.submit({nmrack::Command::Midi,nextDemo+57600,0x80,note,0,0}))
                        throw std::runtime_error("MIDI demo command overflow");
                }
                ++demoChord;nextDemo+=96000;
            }
            if(const auto fault=callback.fault.load(std::memory_order_relaxed))
                throw std::runtime_error("Audio device fault "+std::to_string(fault)+" (1=format, 2=stopped, 3=backend, 4=channels); restart required");
            if(std::chrono::steady_clock::now()-heartbeat>=std::chrono::seconds(2))
            {
                const auto current=callback.frames.load(std::memory_order_relaxed);
                if(current==lastFrames) throw std::runtime_error("Audio callback stalled");
                lastFrames=current;heartbeat=std::chrono::steady_clock::now();
            }
            if(seconds>0&&std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=seconds) break;
            if(simulate)
            {
                simulatedFrames+=uint64_t(block);
                std::this_thread::sleep_until(start+std::chrono::nanoseconds(uint64_t(simulatedFrames*1e9/rate)));
                callback.audioDeviceIOCallbackWithContext(nullptr,0,pointers.data(),channels,block,{});
            }
            else juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
        if(midi) midi->stop();
        if(device) device->stop();
        playback.stop();playback.checkError();
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"seconds="<<elapsed<<" frames="<<callback.frames.load()<<" underruns="<<playback.underruns()
            <<" missing_frames="<<playback.missingFrames()<<" worst_worker_128_ms="<<playback.worstRenderNanoseconds()/1e6
            <<" process_cpu_cores="<<double(std::clock()-cpuStart)/CLOCKS_PER_SEC/elapsed
            <<" rejected_commands="<<playback.rejectedCommands()<<" late_commands="<<playback.lateCommands()
            <<" device_xruns="<<(device?device->getXRunCount():-1)<<'\n';
        return playback.underruns()?1:0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
