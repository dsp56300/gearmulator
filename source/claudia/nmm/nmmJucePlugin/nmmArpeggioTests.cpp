#include "nmmDevice.h"
#include "synthLib/plugin.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>

// A continuous single-note timeline, independent of host callback boundaries.
// All storage and MIDI ordering are prepared before playback; analysis is later.
int main(int argc,char** argv)
{
    try {
        if(argc<3) throw std::runtime_error("usage: nmmArpeggioTests firmware FourVoices.pch [--realtime] [--rate Hz] [--block samples] [--dump prefix]");
        std::string dump;bool realtime=false;unsigned rate=44100,block=64;
        for(int i=3;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--realtime") realtime=true;
            else if(arg=="--dump" && i+1<argc) dump=argv[++i];
            else if(arg=="--rate" && i+1<argc) rate=std::stoul(argv[++i]);
            else if(arg=="--block" && i+1<argc) block=std::stoul(argv[++i]);
            else throw std::runtime_error("Unknown option");
        }
        if(rate<22050 || rate>192000 || !block || block>2048) throw std::runtime_error("Invalid host format");
        auto panel=std::make_shared<nmmJucePlugin::PanelState>();panel->offline=true;
        std::ifstream patch(argv[2]);if(!patch) throw std::runtime_error("Patch unavailable");
        panel->patchText.assign(std::istreambuf_iterator<char>(patch),{});
        nmmJucePlugin::Device device({},argv[1],panel);
        synthLib::Plugin host(&device,[](auto*)->synthLib::Device*{throw std::runtime_error("Unexpected device creation");});
        host.setHostSamplerate(rate,96000);host.setBlockSize(block);
        const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!panel->ready && !panel->failed && std::chrono::steady_clock::now()<readyDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!panel->ready) throw std::runtime_error("Hardware did not become ready");
        struct Event {uint64_t frame;uint8_t status,note,velocity;};
        struct Note {uint64_t frame;unsigned duration,period,pitch;};
        std::vector<Event> events;std::vector<Note> notes;
        uint64_t cursor=rate/2;
        constexpr std::array<unsigned,8> pitches{60,64,67,72,76,71,69,62};
        for(unsigned periodMs:{25u,50u,100u}) for(unsigned gatePercent:{50u,95u,150u,350u}) {
            const unsigned period=rate*periodMs/1000,duration=period*gatePercent/100;
            for(unsigned i=0;i<64;++i) {
                const auto pitch=pitches[i%pitches.size()];
                events.push_back({cursor,0x90,uint8_t(pitch),100});
                events.push_back({cursor+duration,uint8_t(i%3?0x80:0x90),uint8_t(pitch),0});
                notes.push_back({cursor,duration,period,pitch});cursor+=period;
            }
            cursor+=duration+rate/10; // Drain voices before changing gate/tempo.
        }
        std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.frame<b.frame;});
        const auto total=((cursor+rate/2+block-1)/block)*block;
        std::vector<float> recording(total),left(block),right(block);
        synthLib::TAudioInputs input{};synthLib::TAudioOutputs output{};output[0]=left.data();output[1]=right.data();
        // Warm the host/resampler and apply short release before the timeline.
        for(unsigned i=0;i<(rate/4+block-1)/block;++i) host.process(input,output,block,120,0,true);
        panel->values[3]=0;panel->offline=!realtime;
        // Pacing starts with a quiet lead-in; normal realtime runs never wait for the worker.
        const auto start=std::chrono::steady_clock::now();const auto cpuStart=std::clock();
        uint64_t maxLateUs=0;size_t next=0;
        for(uint64_t frame=0;frame<total;frame+=block) {
            while(next<events.size() && events[next].frame<frame+block) {
                const auto& e=events[next++];host.addMidiEvent({synthLib::MidiEventSource::Host,e.status,e.note,e.velocity,uint32_t(e.frame-frame)});
            }
            host.process(input,output,block,120,0,true);
            std::copy(left.begin(),left.end(),recording.begin()+frame);
            if(panel->failed) throw std::runtime_error(panel->status);
            if(realtime) {
                const auto deadline=start+std::chrono::nanoseconds((frame+block)*1000000000/rate);
                const auto late=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-deadline).count();
                if(late>0) maxLateUs=std::max(maxLateUs,uint64_t(late));
                std::this_thread::sleep_until(deadline);
            }
        }
        std::cout<<"cpu_seconds="<<double(std::clock()-cpuStart)/CLOCKS_PER_SEC<<" underruns="<<panel->underruns<<" drops="<<panel->droppedJobs<<" max_host_late_us="<<maxLateUs<<'\n';
        const auto latency=host.getLatencyMidiToOutput();unsigned missing=0;
        if(!dump.empty()) {
            std::ofstream audio(dump+".f32",std::ios::binary);audio.write(reinterpret_cast<const char*>(recording.data()),recording.size()*sizeof(float));
            std::ofstream meta(dump+".csv");meta<<"# rate="<<rate<<" latency="<<latency<<'\n'<<"frame,duration,period,pitch\n";
            for(const auto& n:notes) meta<<n.frame<<','<<n.duration<<','<<n.period<<','<<n.pitch<<'\n';
        }
        for(unsigned index=0;index<notes.size();++index) {
            const auto& n=notes[index];
            // Fit the simultaneously held pitches independently. A short-window
            // single-frequency projection can cancel against a neighbouring note
            // and report a false dropout. Stop before any release changes level.
            const auto begin=n.frame+rate*4/1000;
            auto end=n.frame+std::min(n.period,n.duration)-rate/1000;
            std::array<unsigned,4> active{};unsigned activeCount=0;
            for(unsigned j=0;j<=index;++j) if(notes[j].frame+notes[j].duration>begin) {
                if(activeCount==active.size()) throw std::runtime_error("Arpeggio exceeded four held notes");
                active[activeCount++]=j;end=std::min(end,notes[j].frame+notes[j].duration-rate/1000);
            }
            const unsigned dimensions=1+2*activeCount;
            std::array<double,4> angular{};
            for(unsigned j=0;j<activeCount;++j) angular[j]=2*3.141592653589793*440*std::pow(2.,(int(notes[active[j]].pitch)-69)/12.)/rate;
            double matrix[9][10]{};
            for(uint64_t i=begin;i<end;++i) {
                double row[9]{1}; // Also remove the output coupling's DC offset.
                for(unsigned j=0;j<activeCount;++j) {
                    row[j*2+1]=std::cos(angular[j]*(i-begin));row[j*2+2]=std::sin(angular[j]*(i-begin));
                }
                const auto w=.5-.5*std::cos(2*3.141592653589793*(i-begin)/(end-begin-1));
                for(unsigned j=0;j<dimensions;++j) {
                    for(unsigned k=0;k<dimensions;++k) matrix[j][k]+=w*row[j]*row[k];
                    matrix[j][dimensions]+=w*row[j]*recording.at(i+latency);
                }
            }
            for(unsigned j=0;j<dimensions;++j) {
                unsigned pivot=j;
                for(unsigned k=j+1;k<dimensions;++k) if(std::abs(matrix[k][j])>std::abs(matrix[pivot][j])) pivot=k;
                for(unsigned k=0;k<=dimensions;++k) std::swap(matrix[j][k],matrix[pivot][k]);
                const auto scale=matrix[j][j];
                if(std::abs(scale)<1e-12) throw std::runtime_error("Unresolved arpeggio measurement");
                for(unsigned k=j;k<=dimensions;++k) matrix[j][k]/=scale;
                for(unsigned k=0;k<dimensions;++k) if(k!=j) {
                    const auto multiple=matrix[k][j];
                    for(unsigned l=j;l<=dimensions;++l) matrix[k][l]-=multiple*matrix[j][l];
                }
            }
            // Current note is the last active entry; check both phase components.
            const auto amplitude=std::hypot(matrix[dimensions-2][dimensions],matrix[dimensions-1][dimensions]);
            if(amplitude<.04) {++missing;std::cerr<<"Missing arpeggio note="<<index<<" pitch="<<n.pitch<<" period="<<n.period<<" duration="<<n.duration<<" amplitude="<<amplitude<<'\n';}
        }
        if(missing || panel->underruns || panel->droppedJobs) throw std::runtime_error("Arpeggio failures="+std::to_string(missing));
        std::cout<<"PASS "<<notes.size()<<" successive notes at 10/20/40 Hz, gates 50/95/150/350%, rate="<<rate<<" block="<<block<<" realtime="<<realtime<<'\n';
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
