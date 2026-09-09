#include <ctime>
#include "nmmLib/nmmcapture.h"
#include "nmmDevice.h"
#include "synthLib/plugin.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <iterator>
int main(int argc,char** argv)
{
 try {
    bool arpeggio=false;bool diagnostics=false;bool overlap=false;bool fast=false;bool regions=false;bool realtime=false;unsigned audioQuantum=0,blockLimit=16,captureStart=0;std::string capturePath;
    if(argc<3) throw std::runtime_error("usage: nmmChordTests firmware FourVoices.pch [--realtime] [--fast] [--arpeggio] [--overlap] [--diagnostics] [--capture file.csv] [--capture-start chord] [--audio-driven frames] [--block-limit 16|32|64] [--deadline-graphs]");
    for(int i=3;i<argc;++i) {const std::string arg=argv[i];if((arg=="--deadline-graphs" || arg=="--sample-regions")) regions=true;else if(arg=="--block-limit" && i+1<argc) blockLimit=std::stoul(argv[++i]);else if(arg=="--capture-start" && i+1<argc) captureStart=std::stoul(argv[++i]);else if(arg=="--diagnostics") diagnostics=true;else if(arg=="--overlap") {overlap=true;fast=true;}else if(arg=="--arpeggio") {arpeggio=true;fast=true;}else if(arg=="--fast") fast=true;else if(arg=="--realtime") realtime=true;else if(arg=="--audio-driven" && i+1<argc) audioQuantum=std::stoul(argv[++i]);else if(arg=="--capture" && i+1<argc) capturePath=argv[++i];else throw std::runtime_error("Unknown option");}
    auto capture=capturePath.empty()?nullptr:std::make_unique<nmm::Capture>();
    // Declared before Device: export after the worker has joined, also on failure.
    struct Export {nmm::Capture* capture;std::string path;~Export(){if(capture){std::ofstream out(path);capture->write(out);}}} exporter{capture.get(),capturePath};
    auto panel=std::make_shared<nmmJucePlugin::PanelState>();if(audioQuantum) {panel->audioDrivenFrames=audioQuantum;panel->audioDrivenLinkedJit=true;}panel->jitBlockLimit=blockLimit;panel->jitDeadlineGraphs=regions;panel->offline=!realtime;panel->runtimeDiagnostics=diagnostics;
    std::ifstream f(argv[2]);panel->patchText.assign(std::istreambuf_iterator<char>(f),{});
    nmmJucePlugin::Device device({},argv[1],panel);
    synthLib::Plugin host(&device,[](auto*)->synthLib::Device* {throw std::runtime_error("Invalid device");});
    host.setHostSamplerate(44100,96000);host.setBlockSize(64);
    std::array<float,64> left{},right{};synthLib::TAudioInputs in{};synthLib::TAudioOutputs out{};out[0]=left.data();out[1]=right.data();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!panel->ready && !panel->failed && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(!panel->ready) throw std::runtime_error("Hardware did not become ready");
    // Capture into fixed storage while pacing callbacks. Spectral analysis belongs
    // after playback: trig loops on the host thread can manufacture underruns.
    std::array<uint64_t,256> underruns{},drops{};
    auto recordings=std::make_unique<std::array<std::array<float,70*64>,256>>();
    const auto start=std::chrono::steady_clock::now();uint64_t frames=0,maxHostLateUs=0;
    auto pump=[&]{
        if(realtime) {
            const auto late=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start-std::chrono::nanoseconds(frames*1000000000/44100)).count();
            if(late>0) maxHostLateUs=std::max(maxHostLateUs,uint64_t(late));
        }
        host.process(in,out,64,120,0,true);frames+=64;if(panel->failed) throw std::runtime_error(panel->status);if(realtime) std::this_thread::sleep_until(start+std::chrono::nanoseconds(frames*1000000000/44100));};
    for(unsigned i=0;i<100;++i) pump();
    panel->values[3]=0;for(unsigned i=0;i<100;++i) pump();
    const unsigned repeats=fast?256:64;
    const unsigned settleBlocks=fast?10:20,recordBlocks=fast?20:70;
    auto transpose=[&](unsigned repeat){return !arpeggio && fast && repeat%2?12:0;};
    const std::array<int,8> arpNotes{60,64,67,72,79,76,71,67};
    struct Notes {std::array<int,4> data;unsigned count;const int* begin() const{return data.data();}const int* end() const{return data.data()+count;}};
    auto notesFor=[&](unsigned repeat){return arpeggio?Notes{{arpNotes[repeat%arpNotes.size()],0,0,0},1}:Notes{{60,64,67,71},4};};
    const auto playbackCpu=std::clock();
    for(unsigned repeat=0;repeat<repeats;++repeat)
    {
        if(repeat==captureStart) panel->capture=capture.get();
        for(auto note:notesFor(repeat)) host.addMidiEvent({synthLib::MidiEventSource::Host,0x90,uint8_t(note+transpose(repeat)),100,uint32_t(repeat%64)});
        // New pitches arrive before old note-offs: force native voice stealing.
        if(overlap && repeat) for(auto note:notesFor(repeat-1))
            host.addMidiEvent({synthLib::MidiEventSource::Host,0x80,uint8_t(note+transpose(repeat-1)),0,uint32_t(repeat%64)});
        for(unsigned i=0;i<settleBlocks;++i) pump();
        for(unsigned i=0;i<recordBlocks;++i) {pump();std::copy(left.begin(),left.end(),(*recordings)[repeat].begin()+i*64);}
        if(!overlap || repeat+1==repeats) for(auto note:notesFor(repeat)) host.addMidiEvent({synthLib::MidiEventSource::Host,uint8_t(fast && repeat%3==0?0x90:0x80),uint8_t(note+transpose(repeat)),0,uint32_t(repeat%64)});
        for(unsigned i=0;i<repeat%8;++i) pump();
        underruns[repeat]=panel->underruns;drops[repeat]=panel->droppedJobs;
    }
    for(unsigned i=0;i<20;++i) pump();
    std::cout<<"playback_cpu_seconds="<<double(std::clock()-playbackCpu)/CLOCKS_PER_SEC<<'\n';
    std::cout<<"underruns="<<panel->underruns<<" dropped_jobs="<<panel->droppedJobs<<'\n';
    std::cout<<"host_late_max_us="<<maxHostLateUs;
    if(diagnostics) std::cout<<" worker_job_max_us="<<panel->workerJobMaxUs<<" worker_gap_max_us="<<panel->workerGapMaxUs
        <<" render_max_us="<<panel->renderMaxUs<<" control_max_us="<<panel->controlMaxUs
        <<" first_miss_target="<<panel->firstMissTarget<<" first_miss_available="<<panel->firstMissAvailable
        <<" first_miss_job_depth="<<panel->firstMissJobDepth;
    std::cout<<'\n';
    unsigned missing=0;
    for(unsigned repeat=0;repeat<repeats;++repeat)
    {
        const auto& samples=(*recordings)[repeat];const unsigned sampleCount=recordBlocks*64;
        for(auto note:notesFor(repeat))
        {
            double re=0,im=0;const double freq=440*std::pow(2.,(note+transpose(repeat)-69)/12.);
            for(unsigned i=0;i<sampleCount;++i) {const double phase=2*3.141592653589793*freq*i/44100,w=.5-.5*std::cos(2*3.141592653589793*i/(sampleCount-1));re+=samples[i]*w*std::cos(phase);im+=samples[i]*w*std::sin(phase);}
            const double amp=4*std::hypot(re,im)/sampleCount;
            // OS 3.03b protects the lowest held note (10798c..10799c).
            // Ascending overlapping chords steal their own first new note;
            // releasing the protected old note then leaves three voices.
            const bool stolen=!arpeggio && overlap && repeat%2 && note==60;
            if(stolen ? amp>.01 : amp<.04) {
                ++missing;std::cerr<<"Unexpected host voice repeat="<<repeat<<" note="<<note+transpose(repeat)
                    <<" expected="<<(stolen?"stolen":"audible")<<" amplitude="<<amp
                    <<" underruns="<<underruns[repeat]<<" dropped_jobs="<<drops[repeat]<<'\n';
            }
        }
    }
    if(missing) throw std::runtime_error("Missing voice observations="+std::to_string(missing));
    if(panel->underruns || panel->droppedJobs) throw std::runtime_error("Chord playback lost audio/jobs: underruns="+std::to_string(panel->underruns.load())+" dropped_jobs="+std::to_string(panel->droppedJobs.load()));
    std::cout<<"PASS "<<repeats<<" short-release chords through plugin/resampler at 44100/64; underruns="<<panel->underruns<<" drops="<<panel->droppedJobs<<" arpeggio="<<arpeggio<<" overlap="<<overlap<<" fast="<<fast<<" realtime="<<realtime<<'\n';
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
