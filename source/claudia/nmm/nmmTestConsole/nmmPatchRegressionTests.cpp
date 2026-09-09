#include "nmmLib/nmmhardware.h"
#include "nmmLib/nmmeditorstate.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    struct Window
    {
        double sum[2]{},squares[2]{},peak=0;
        size_t count=0;
        void add(const std::array<float,2>& frame)
        {
            for(unsigned ch=0;ch<2;++ch)
            {
                require(std::isfinite(frame[ch]),"Non-finite patch output");
                peak=std::max(peak,std::abs(double(frame[ch])));
                sum[ch]+=frame[ch];squares[ch]+=double(frame[ch])*frame[ch];
            }
            ++count;
        }
        double ac() const
        {
            double result=0;
            for(unsigned ch=0;ch<2;++ch) result=std::max(result,std::sqrt(std::max(0.0,squares[ch]/count-std::pow(sum[ch]/count,2))));
            return result;
        }
    };
    Window play(nmm::Hardware& hw,bool four,std::vector<double>& timings)
    {
        for(auto note:{60,64,67,71}) {hw.sendMidi(0x90,note,100);if(!four) break;}
        Window result;
        const auto start=hw.timing();
        for(unsigned block=0;block<750;++block)
        {
            const auto begin=std::chrono::steady_clock::now();
            const auto audio=hw.render(128,1000000);
            timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
            for(auto frame:audio) result.add(frame);
        }
        const auto end=hw.timing();
        const auto frames=end.frames-start.frames;
        require(std::abs(int64_t(end.cycles-start.cycles)-int64_t(frames*864))<3456,"ESSI maintains 864 DSP clocks per stereo frame");
        // X:1 is reset by the foreground after every four audio IRQs. It is
        // a control-rate work counter, not a monotonically increasing clock.
        require(end.controlSampleCounter<=8,"Foreground control processing keeps pace with sample IRQs");
        hw.sendMidi(0xb0,123,0);
        return result;
    }
}
int main(int argc,char** argv)
{
    try
    {
        require(argc>=3 && argc<=13,"usage: nmmPatchRegressionTests firmware.bin patch-directory [--native] [--linked-jit] [--audio-driven frames] [--cooperative] [--continuations] [--deadline-graphs] [--block-limit 16|32|64] [--diagnostics]");
        bool regions=false;unsigned blockLimit=16;bool diagnostics=false;bool continuations=false;bool native=false,linked=false,threaded=true;unsigned audioQuantum=0;
        for(int i=3;i<argc;++i) {const std::string flag=argv[i];if((flag=="--deadline-graphs" || flag=="--sample-regions")) regions=true;else if(flag=="--block-limit" && i+1<argc) blockLimit=std::stoul(argv[++i]);else if(flag=="--diagnostics") diagnostics=true;else if(flag=="--continuations") {continuations=true;native=true;linked=true;}else if(flag=="--native") native=true;else if(flag=="--linked-jit") {native=true;linked=true;}else if(flag=="--cooperative") threaded=false;else if(flag=="--audio-driven" && i+1<argc) {audioQuantum=std::stoul(argv[++i]);native=true;}else throw std::runtime_error("Unknown patch regression option");}
        for(const auto* name:{"BasicOsc","FourVoices","101","SimpleSqr1","SimpleSynth02","Gong01","BDrm"})
        {
            std::cout<<"START patch="<<name<<" block_limit="<<blockLimit<<std::endl;
            const auto loadStart=std::chrono::steady_clock::now();
            nmm::Hardware hw(argv[1]);hw.setJitBlockLimit(blockLimit,regions);hw.setAudioDrivenExecution(audioQuantum,threaded);hw.setDeadlineLinkedJit(linked,continuations);hw.boot(30000000);
            hw.loadPatch(std::string(argv[2])+"/"+name+".pch",30000000);
            const auto loadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-loadStart).count();
            if(!native || diagnostics) hw.enableRuntimeDiagnostics();
            hw.setMasterVolume(100);hw.render(12000,10000000);
            std::vector<double> timings;
            const bool four=std::string(name)=="FourVoices";
            const auto original=play(hw,four,timings);
            require(original.ac()>0.0001 && original.peak<1,"Patch must render bounded audible audio, including transient-only patches");
            hw.render(12000,10000000); // consume all-notes-off before serializing held-note state
            std::vector<uint8_t> snapshot;
            const auto beforeSnapshot=hw.timing();
            for(const auto& packet:hw.exportEditorPatch()) snapshot.insert(snapshot.end(),packet.begin(),packet.end());
            require(hw.timing().cycles==beforeSnapshot.cycles && hw.timing().frames==beforeSnapshot.frames,"Snapshot must not advance DSP/audio time");
            require(nmm::validEditorPatch(snapshot),"Native snapshot framing/checksum");
            // Reuse the same running DSP/JIT for a different graph, then restore.
            // This catches stale generated code and loop endpoints across uploads.
            nmm::Hardware seed(argv[1]);seed.setJitBlockLimit(blockLimit,regions);seed.setDeadlineLinkedJit(linked,continuations);seed.boot(30000000);seed.initializePatch(30000000);
            std::vector<uint8_t> initial;
            for(const auto& packet:seed.exportEditorPatch()) initial.insert(initial.end(),packet.begin(),packet.end());
            const auto replaceStart=std::chrono::steady_clock::now();
            std::cout<<"REPLACE patch="<<name<<std::endl;
            hw.restoreEditorPatch(initial);hw.render(12000,10000000);
            hw.restoreEditorPatch(snapshot);hw.render(12000,10000000);
            const auto replaceMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-replaceStart).count();
            const auto restored=play(hw,four,timings);
            require(restored.ac()>original.ac()*.25 && restored.ac()<original.ac()*4,"Native graph replacement/restore retains sound");
            hw.setMasterVolume(0);hw.render(96000,10000000);
            Window silent;for(auto frame:hw.render(12000,10000000)) silent.add(frame);
            require(silent.ac()<0.00001,"Native master mute across patch families");
            std::sort(timings.begin(),timings.end());
            std::cout<<name<<" block_limit="<<blockLimit<<" load_ms="<<loadMs<<" replace_ms="<<replaceMs<<" ac="<<original.ac()<<" restored="<<restored.ac()<<" peak="<<original.peak
                     <<" deadline_graphs="<<regions<<" effective_block_limit="<<(regions?16:blockLimit)
                     <<" backpressure_yields="<<hw.timing().backpressureYields<<" max_queued_audio="<<hw.timing().maxQueuedAudio
                     <<" peripheral_boundary_overrun_ticks="<<hw.timing().maxPeripheralBoundaryOverrun
                     <<" max_dsp_block_cycles="<<hw.timing().maxBlockCycles<<" max_irq_acceptance_cycles="<<hw.timing().maxIrqAcceptanceCycles
                     <<" max_control_backlog="<<hw.timing().maxControlBacklog<<" max_pending_irqs="<<hw.timing().maxPendingIrqs
                     <<" block_p99_ms="<<timings[timings.size()*99/100]<<" worst_ms="<<timings.back()<<'\n';
        }
        std::cout<<"PASS patch matrix: oscillator, four voices, transient envelope, morph/filter, percussion, native graph replacement and mute\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
