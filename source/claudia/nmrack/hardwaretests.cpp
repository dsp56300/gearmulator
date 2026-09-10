#include "hardware.h"
#include "dsp56kBase/logging.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool v,const char* message) {if(!v) throw std::runtime_error(message);}
static std::vector<nmrack::AudioFrame> render(const std::string& rom,bool linked,bool words=false,unsigned fixedChunk=0)
{
    nmrack::Hardware hw(rom,{false,linked,true,words});hw.setStepBudget(450000000);
    hw.boot();require(hw.status().tablesVerified&&hw.status().mainLoopVisits>100,"Full Rack boot");
    hw.initializeTone();hw.advance(96000);
    std::vector<nmrack::AudioFrame> frames(9600);
    const unsigned chunks[]{1,7,63,64,127,128,255};
    unsigned done=0,index=0;
    while(done<frames.size())
    {
        const auto n=std::min(unsigned(frames.size()-done),fixedChunk?fixedChunk:chunks[index++%7]);
        hw.renderDiagnosticMix(frames.data()+done,n);done+=n;
    }
    require(hw.status().mixFrames>=frames.size()&&hw.status().mixFrames<frames.size()+64,"Bounded audio target surplus");
    double maxRms=0;
    std::array<double,4> channelRms{};
    for(unsigned ch=0;ch<4;++ch)
    {
        double sum=0,squares=0;
        for(auto f:frames) {require(std::isfinite(f[ch]),"Finite mixer output");sum+=f[ch];squares+=double(f[ch])*f[ch];}
        channelRms[ch]=std::sqrt(std::max(0.0,squares/frames.size()-std::pow(sum/frames.size(),2)));
        maxRms=std::max(maxRms,channelRms[ch]);
    }
    if(words)
    {
        const auto s=hw.status();
        require(s.serialWords>1000000&&s.acceptedSerialWords>s.serialWords-20000,"Word edges reach enabled receivers");
        require(s.maxReceiverLeadFrames<.125,"Receiver lead stays below one serial word (bounded JIT overshoot)");
        require(channelRms[1]>.03&&channelRms[1]<.04,"Word-edge tone reaches diagnostic output 2");
        require(channelRms[0]<1e-8&&channelRms[2]<1e-8&&channelRms[3]<1e-8,"No AC leakage to other diagnostic outputs");
        double mean=0;for(const auto& f:frames) mean+=f[1];mean/=frames.size();
        unsigned crossings=0;
        for(unsigned n=1;n<frames.size();++n) if(frames[n-1][1]<mean&&frames[n][1]>=mean) ++crossings;
        require(crossings>=32&&crossings<=34,"Approximately 330 Hz in 0.1 second capture");
        std::cout<<"Word-edge tone: diagnostic output 2, AC RMS="<<channelRms[1]<<", rising crossings="<<crossings<<'\n';
    }
    else require(maxRms>.01&&maxRms<.2,"AC signal survives the four-DSP chain (not merely DAC bias)");
    return frames;
}
int main(int argc,char** argv)
{
    try
    {
        require(argc==2,"Usage: nmrackHardwareTests firmware");
        Logging::setLogFunc([](const std::string&){});
        for(bool words:{false,true}) for(auto limit:{uint64_t(1),uint64_t(1000000)})
        {
            nmrack::Hardware limited(argv[1],{false,true,true,words});limited.setStepBudget(limit);
            bool stopped=false;try {limited.boot();} catch(const std::runtime_error&) {stopped=true;}
            require(stopped,"Boot execution budget is enforced");
        }
        const auto linked=render(argv[1],true);
        const auto unlinked=render(argv[1],false);
        require(linked==unlinked,"Checked-link JIT matches reference diagnostic mixer samples");
        const auto wordLinked=render(argv[1],true,true);
        require(wordLinked==render(argv[1],false,true,128),"Word-edge execution matches across JIT modes and buffer targets");
        require(wordLinked==render(argv[1],true,true,7),"Word-edge execution matches small fixed buffer targets");
        std::cout<<"PASS nmrack diagnostic hardware: four DSPs, native tone, word-edge output 2, bounded variable-size audio targets, bit-identical linked/unlinked JIT\n";
        std::cout<<"Physical serial phase, codec output mapping and polyphony remain unvalidated\n";
        return 0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
