#pragma once
#include <array>
#include <cstdint>
#include <ostream>
namespace nmm
{
    // Allocate and zero before playback. One producer (emulator worker); inspect
    // only after detaching/stopping that worker. No locks, clocks or I/O on push.
    struct Capture
    {
        enum Kind:uint32_t {Midi=1,McuEntry,HostWord,Voice,Audio,Burst,Worker,Service,DspReceive};
        struct Event {uint64_t cycles=0,frame=0,mcuCycles=0;Kind kind=Midi;std::array<uint32_t,5> data{};};
        static constexpr size_t Capacity=32768;
        std::array<Event,Capacity> events{};
        size_t count=0;bool full=false;
        uint64_t startCycles=0,startFrames=0,bursts=0,burstCycles=0,maxBurstCycles=0;
        void push(uint64_t cycles,uint64_t frame,Kind kind,std::array<uint32_t,5> data={},uint64_t mcuCycles=0) noexcept
        {
            if(count==Capacity) {full=true;return;}
            events[count++]={cycles,frame,mcuCycles,kind,data};
        }
        void write(std::ostream& out) const
        {
            out<<"# full="<<full<<" bursts="<<bursts<<" burst_cycles="<<burstCycles<<" max_burst_cycles="<<maxBurstCycles<<'\n';
            out<<"cycles,frame,mcu_cycles,kind,a,b,c,d,e\n";
            for(size_t i=0;i<count;++i) {const auto& e=events[i];out<<e.cycles<<','<<e.frame<<','<<e.mcuCycles<<','<<e.kind;for(auto v:e.data) out<<','<<v;out<<'\n';}
        }
    };
}
