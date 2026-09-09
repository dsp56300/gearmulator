#include "nmmLib/nmmcapture.h"
#include "nmmLib/nmmaudioworker.h"
#include "nmmLib/nmmhardware.h"
#include "nmmLib/nmmpatch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

int main(int argc,char** argv)
{
    try
    {
        if(argc<2 || argc>9) throw std::runtime_error("usage: nmmRuntimeTests firmware.bin [audio-driven-frames] [cooperative] [block-limit] [--linked-only] [--deadline-graphs] [--dma-trace prefix]");
        const unsigned audioQuantum=argc>=3?std::stoul(argv[2]):0;
        const bool threaded=argc<4;
        if(argc>=4 && std::string(argv[3])!="cooperative") throw std::runtime_error("Unknown scheduler option");
        const unsigned blockLimit=argc>=5?std::stoul(argv[4]):16;
        bool linkedOnly=false,regions=false;std::string tracePrefix;
        for(int i=5;i<argc;++i) {
            const std::string flag=argv[i];
            if(flag=="--linked-only") linkedOnly=true;
            else if((flag=="--deadline-graphs" || flag=="--sample-regions")) regions=true;
            else if(flag=="--dma-trace" && i+1<argc) tracePrefix=argv[++i];
            else throw std::runtime_error("Unknown comparison option");
        }
        if(audioQuantum)
        {
            unsigned completed=0;bool fail=false;
            {
                nmm::AudioWorker worker([&]{if(fail) throw std::runtime_error("Expected worker failure");++completed;});
                for(unsigned i=0;i<32;++i) {worker.start();worker.wait();}
                fail=true;worker.start();bool caught=false;
                try {worker.wait();} catch(const std::runtime_error&) {caught=true;}
                if(!caught || completed!=32) throw std::runtime_error("Worker exception handoff failed");
                fail=false;worker.start(); // Destructor must finish this grant.
            }
            if(completed!=33) throw std::runtime_error("Worker shutdown lost a pending grant");
        }
        nmm::Patch patch;patch.name="Stereo input";
        patch.modules={{0,1,2},{0,2,4}};
        patch.parameters={{0,2,0,127},{0,2,1,0},{0,2,2,0}};
        patch.cables={{0,{2,0,1,0x40,0,0}},{0,{2,1,1,0x41,0,0}}};
        std::vector<std::array<float,2>> input(96000),reference(96000),split(96000);
        // Independent channel impulses/tone windows catch a swap or crosstalk.
        for(unsigned i=0;i<input.size();++i)
            input[i]=i<48000?std::array<float,2>{float(.2*std::sin(i*.031)),0}:std::array<float,2>{0,float(.1*std::sin(i*.047))};
        for(unsigned pass=0;pass<8;++pass)
        {
            if(linkedOnly && (pass==2 || pass==3 || pass>=6)) continue;
            nmm::Hardware hw(argv[1]);hw.setJitBlockLimit(pass>=2?blockLimit:16,pass>=2 && regions);hw.setAudioDrivenExecution(audioQuantum,threaded);hw.setDeadlineLinkedJit(pass>=4,pass>=6);hw.boot(30000000);hw.loadPatch(patch,30000000);hw.setMasterVolume(127);
            auto capture=std::make_unique<nmm::Capture>();
            if(!audioQuantum || !threaded) {if(pass<2) hw.enableRuntimeDiagnostics();else hw.setCapture(capture.get());}
            if(!tracePrefix.empty()) hw.enableDmaBoundaryTrace(131072);
            auto& output=pass?split:reference;
            for(unsigned begin=0,block=0;begin<input.size();++block)
            {
                const unsigned sizes[]{1,37,255,128,7,513};
                const auto size=std::min<unsigned>((pass&1)?sizes[block%6]:256,input.size()-begin);
                hw.renderInto(output.data()+begin,size,1000000,input.data()+begin);begin+=size;
                if(pass && block==10)
                {
                    const auto before=hw.timing();std::ostringstream map;hw.dumpJitMap(map);
                    const auto after=hw.timing();
                    if(map.str().empty() || before.cycles!=after.cycles || before.frames!=after.frames)
                        throw std::runtime_error("JIT map capture changed execution or returned no blocks");
                }
            }
            if(!tracePrefix.empty()) {
                std::ofstream out(tracePrefix+"-"+std::to_string(pass)+".csv");
                if(!out) throw std::runtime_error("Cannot write DMA boundary trace");
                hw.writeDmaBoundaryTrace(out);
            }
            if(pass && reference!=split)
            {
                size_t first=reference.size(),different=0;double maxError=0,squares=0;
                for(size_t i=0;i<reference.size();++i) for(unsigned ch=0;ch<2;++ch) {
                    const double error=double(split[i][ch])-reference[i][ch];
                    if(error!=0) {first=std::min(first,i);++different;}
                    maxError=std::max(maxError,std::abs(error));squares+=error*error;
                }
                std::cerr<<"MISMATCH block_limit="<<blockLimit<<" pass="<<pass<<" first_frame="<<first
                         <<" different_samples="<<different<<" max_error="<<maxError
                         <<" rms_error="<<std::sqrt(squares/(2*reference.size()))<<'\n';
                throw std::runtime_error("Native/diagnostic execution or host block boundaries change input audio");
            }
            if((!audioQuantum || !threaded) && pass>=2 && (!capture->count || !capture->bursts)) throw std::runtime_error("Native capture empty");
            if(audioQuantum && threaded && pass==0)
            {
                bool rejected=false;
                try {hw.setCapture(capture.get());} catch(const std::runtime_error&) {rejected=true;}
                if(!rejected) throw std::runtime_error("Concurrent capture accepted");
                rejected=false;
                try {hw.enableRuntimeDiagnostics();} catch(const std::runtime_error&) {rejected=true;}
                if(!rejected) throw std::runtime_error("Concurrent diagnostics accepted");
            }
            const auto timing=hw.timing();
            std::cout<<"split="<<pass<<" max_dsp_block_cycles="<<timing.maxBlockCycles<<" max_irq_acceptance_cycles="<<timing.maxIrqAcceptanceCycles
                     <<" max_control_backlog="<<timing.maxControlBacklog<<'\n';
        }
        if(reference!=split) throw std::runtime_error("Input changes with host block boundaries");
        for(unsigned ch=0;ch<2;++ch)
        {
            double energy=0,other=0,sum=0,otherSum=0;
            const unsigned begin=ch?60000:12000;
            for(unsigned i=begin;i<begin+24000;++i) {energy+=double(reference[i][ch])*reference[i][ch];other+=double(reference[i][1-ch])*reference[i][1-ch];sum+=reference[i][ch];otherSum+=reference[i][1-ch];}
            const auto rms=std::sqrt(std::max(0.0,energy/24000-std::pow(sum/24000,2))),leak=std::sqrt(std::max(0.0,other/24000-std::pow(otherSum/24000,2)));
            std::cout<<"input channel="<<ch<<" RMS="<<rms<<" other_channel_RMS="<<leak<<'\n';
            if(rms<.02 || rms>.15 || leak>.0001) throw std::runtime_error("Audio input silent, swapped, unbounded or leaking");
        }
        std::cout<<"PASS stereo ESSI/DMA input, channel isolation and block-size invariance\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
