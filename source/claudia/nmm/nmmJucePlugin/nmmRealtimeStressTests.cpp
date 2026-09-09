// Paced worker stress; requires user-supplied firmware. No audio device needed.
#include "nmmDevice.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc,char** argv)
{
    try
    {
        if(argc<5 || argc>8) throw std::runtime_error("Usage: nmmRealtimeStressTests firmware.bin FourVoices.pch seconds instances [native-block-size] [audio-driven-frames] [cooperative]");
        const unsigned seconds=std::stoul(argv[3]),instances=std::stoul(argv[4]),blockSize=argc>=6?std::stoul(argv[5]):128;
        const unsigned audioQuantum=argc>=7?std::stoul(argv[6]):0;
        const bool threaded=argc!=8;
        if(argc==8 && std::string(argv[7])!="cooperative") throw std::runtime_error("Unknown scheduler option");
        if(audioQuantum>64) throw std::runtime_error("Audio-driven frames 0..64");
        if(blockSize<64 || blockSize>4096) throw std::runtime_error("Block size 64..4096");
        const unsigned blocks=seconds*96000/blockSize,perSecond=96000/blockSize,editPeriod=std::max(1u,24000/blockSize);
        if(seconds<10 || seconds>3600 || !instances || instances>8) throw std::runtime_error("Stress duration 10..3600 seconds, instances 1..8");
        std::ifstream file(argv[2]);if(!file) throw std::runtime_error("Missing patch");
        const std::string patch{std::istreambuf_iterator<char>(file),{}};
        std::vector<std::shared_ptr<nmmJucePlugin::PanelState>> panels;
        std::vector<std::unique_ptr<nmmJucePlugin::Device>> devices;
        for(unsigned i=0;i<instances;++i)
        {
            auto panel=std::make_shared<nmmJucePlugin::PanelState>();panel->patchText=patch;if(argc>=7) {panel->audioDrivenFrames=audioQuantum;panel->audioDrivenThreaded=threaded;panel->audioDrivenLinkedJit=audioQuantum!=0;}
            devices.push_back(std::make_unique<nmmJucePlugin::Device>(synthLib::DeviceCreateParams{},argv[1],panel));panels.push_back(panel);devices.back()->setProcessingBlockSize(blockSize);
        }
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        for(auto p:panels)
        {
            while(!p->ready && !p->failed && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if(!p->ready) {std::lock_guard<std::mutex> lock(p->mutex);throw std::runtime_error(p->status);}
        }
        std::vector<float> left(blockSize),right(blockSize);synthLib::TAudioOutputs out{};out[0]=left.data();out[1]=right.data();
        synthLib::TAudioInputs in{};std::vector<synthLib::SMidiEvent> midi,response;midi.reserve(8);
        std::vector<double> squares(instances), callbackTimes;callbackTimes.reserve(blocks*instances);
        std::vector<uint64_t> previousUnderruns(instances);
        double maxHostLateUs=0;unsigned misses=0;
        const auto start=std::chrono::steady_clock::now();const auto cpuStart=std::clock();
        for(unsigned block=0;block<blocks;++block)
        {
            maxHostLateUs=std::max(maxHostLateUs,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()-double(block)*blockSize*1000000/96000);
            midi.clear();const auto phase=block%(5*perSecond);
            if(phase==0 || phase==4*perSecond) for(auto note:{60,64,67,71}) midi.emplace_back(synthLib::MidiEventSource::Host,phase==0?0x90:0x80,note,phase==0?100:0,32);
            for(unsigned i=0;i<instances;++i)
            {
                if(block%editPeriod==editPeriod/2)
                {
                    std::vector<uint8_t> edit{0xf0,0x33,0x4c,6,1,64,1,4,0,uint8_t((block/editPeriod)%2?110:127)};
                    unsigned sum=0;for(auto b:edit) sum+=b;edit.push_back(sum&127);edit.push_back(0xf7);
                    if(!panels[i]->editor->receive(edit.data(),edit.size())) throw std::runtime_error("Editor queue overflow");
                }
                if(block%(10*perSecond)==5*perSecond)
                {
                    // Native flash writes plus library refresh must coexist with playback.
                    for(const auto payload: {std::vector<uint8_t>{65,11,0,0,98},std::vector<uint8_t>{65,20,0,96}})
                    {
                        std::vector<uint8_t> message{0xf0,0x33,0x5c,6};message.insert(message.end(),payload.begin(),payload.end());
                        unsigned sum=0;for(auto b:message) sum+=b;message.push_back(sum&127);message.push_back(0xf7);
                        if(!panels[i]->editor->receive(message.data(),message.size())) throw std::runtime_error("Library queue overflow");
                    }
                }
                if(block%perSecond==perSecond/3)
                {
                    const int variation=(block/perSecond)%2;
                    panels[i]->values[0]=126+variation;
                    panels[i]->values[1]=120+variation;
                    panels[i]->values[2]=1+variation;
                    panels[i]->values[3]=20+variation;
                }
                const auto before=std::chrono::steady_clock::now();devices[i]->process(in,out,blockSize,midi,response);
                const auto underruns=panels[i]->underruns.load();
                if(underruns!=previousUnderruns[i] && misses++<10) std::cout<<"underrun_at_frame="<<uint64_t(block)*blockSize<<" instance="<<i<<" lost="<<underruns-previousUnderruns[i]<<'\n';
                previousUnderruns[i]=underruns;
                callbackTimes.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-before).count());
                // Model the MIDI service consuming replies; otherwise a long
                // test fills its own undrained output queue, unlike the product.
                {std::lock_guard<std::mutex> lock(panels[i]->editor->mutex);
                 if(panels[i]->editor->rejected) throw std::runtime_error("Editor transport overflow");
                 panels[i]->editor->output.clear();}
                for(auto value:right) {if(!std::isfinite(value) || std::abs(value)>1.01) throw std::runtime_error("Unbounded audio");squares[i]+=double(value)*value;}
            }
            if(block && block%(30*perSecond)==0) std::cout<<"progress_seconds="<<uint64_t(block)*blockSize/96000<<std::endl;
            std::this_thread::sleep_until(start+std::chrono::microseconds(uint64_t(block+1)*blockSize*1000000/96000));
        }
        const auto cpu=double(std::clock()-cpuStart)/CLOCKS_PER_SEC;
        std::sort(callbackTimes.begin(),callbackTimes.end());
        std::cout<<"seconds="<<seconds<<" instances="<<instances<<" native_block="<<blockSize<<" latency_frames="<<devices[0]->getInternalLatencyMidiToOutput()<<" cpu_seconds="<<cpu<<" cpu_percent_one_core="<<100*cpu/seconds
                 <<" max_host_lateness_us="<<maxHostLateUs<<" callback_p99_us="<<callbackTimes[callbackTimes.size()*99/100]<<" callback_max_us="<<callbackTimes.back()<<'\n';
        bool pass=true;
        for(unsigned i=0;i<instances;++i)
        {
            const auto rms=std::sqrt(squares[i]/(blocks*blockSize));
            std::cout<<"instance="<<i<<" rms="<<rms<<" underrun_frames="<<panels[i]->underruns<<" dropped_jobs="<<panels[i]->droppedJobs<<'\n';
            bool stored;{std::lock_guard<std::mutex> lock(panels[i]->mutex);stored=panels[i]->bank.size()==99 && !panels[i]->bank[98].name.empty();}
            std::cout<<"native_slot_99="<<stored<<'\n';
            pass &= rms>.005 && stored && panels[i]->underruns==0 && panels[i]->droppedJobs==0 && !panels[i]->failed;
        }
        if(!pass) throw std::runtime_error("Realtime stress failed");
        std::cout<<"PASS paced four-voice playback with continuous native editor edits\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
