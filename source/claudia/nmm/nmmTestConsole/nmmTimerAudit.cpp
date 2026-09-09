// Mandatory offline timer regressions. No firmware or audio device required.
#include "dsp56kEmu/dsp.h"
#include <iostream>

int main()
{
    dsp56k::DefaultMemoryValidator validator;
    dsp56k::Peripherals56303 peripherals;
    dsp56k::PeripheralsNop nop;
    dsp56k::Memory memory(validator,0x20000,0x200000,0x200000);
    dsp56k::DSP dsp(memory,&peripherals,&nop);
    auto& timers=peripherals.getTimers();
    timers.setDSP(&dsp,true);
    timers.setTimerUpdateInterval(1);
    unsigned failures=0;
    const auto check=[&](const char* name,unsigned actual,unsigned expected)
    {
        std::cout<<(actual==expected?"PASS ":"MISMATCH ")<<name
                 <<" actual="<<actual<<" expected="<<expected<<'\n';
        failures+=actual!=expected;
    };
    const auto ticks=[&](unsigned n){dsp.fastForward(0,2ull*n);timers.exec();};
    constexpr unsigned tcf=1u<<dsp56k::Timer::M_TCF;
    const auto reset=[&]()
    {
        timers.writeTCSR(0,0);
        timers.writeTCSR(0,timers.readTCSR(0)&(3u<<20)); // clear only set flags
        timers.writeTCR(0,0);
        timers.writeTLR(0,0xffffab);
        timers.writeTCPR(0,0xffffd4);
    };
    reset();
    timers.writeTCSR(0,0xa70);
    timers.writeTCSR(0,0xa71); // exact Nord PWM enable sequence, no TCR write
    ticks(1);
    check("PWM first clock loads TLR",timers.readTCR(0),0xffffab);

    reset();
    timers.writeTCSR(0,0xa71);
    timers.writeTCR(0,0xffffab); // isolate steady-state behavior from startup
    ticks(85*5+12);
    check("PWM delayed poll phase",timers.readTCR(0),0xffffab+12);
    check("PWM crossed compare before wrap",bool(timers.readTCSR(0)&tcf),1);

    reset();
    timers.writeTCSR(0,0xa71);
    timers.writeTCR(0,0xffffd3);
    ticks(1);
    check("PWM exact compare",bool(timers.readTCSR(0)&tcf),1);
    timers.writeTCSR(0,0xa71|tcf);
    ticks(1);
    check("PWM no repeated compare after flag clear",bool(timers.readTCSR(0)&tcf),0);

    timers.writeTCSR(0,0);
    timers.writeTCSR(0,timers.readTCSR(0)&(3u<<20));
    timers.writeTCSR(0,3u<<20);
    check("write-one-clear cannot set cleared flags",timers.readTCSR(0)&(3u<<20),0);

    // Time accumulated while disabled must not be credited to a newly enabled timer.
    reset();
    dsp.fastForward(0,1000);
    timers.writeTCSR(0,0xa71);
    ticks(1);
    check("enable excludes earlier disabled time",timers.readTCR(0),0xffffab);
    ticks(41);
    check("compare at programmed distance after preload",bool(timers.readTCSR(0)&tcf),1);
    timers.writeTCSR(0,0xa71); // zero preserves pending status
    check("write zero preserves TCF",bool(timers.readTCSR(0)&tcf),1);
    timers.writeTCSR(0,0xa70);
    check("disable clears flags",timers.readTCSR(0)&(3u<<20),0);

    reset();
    timers.writeTCSR(0,0xa71);timers.writeTCR(0,0xffffab);
    ticks(0x1000000+85*5+12);
    check("PWM phase beyond full 24-bit span",timers.readTCR(0),
          0xffffab+(0x1000000+85*5+12)%85);

    reset();
    timers.setDSP(&dsp,false); // other devices retain instruction-clock default
    timers.writeTLR(0,10);timers.writeTCSR(0,1);
    dsp.fastForward(2,0);timers.exec();
    check("default clock first tick",timers.readTCR(0),10);
    dsp.fastForward(10,0);timers.exec();
    check("default instruction clock retained",timers.readTCR(0),15);
    timers.writeTCSR(0,0);timers.setDSP(&dsp,true);

    // Compare batched advancement with an independent, one-tick reference.
    // Include wrap, reload-on-next-tick, TLR > TCPR, and varied polling phases.
    for(const auto control:{0x71u,0x271u,0x1u,0x201u})
    for(const auto load:{0u,0xffffabu,0xfffffeu})
    for(const auto compare:{0u,0xffffd4u,0xffffffu})
    {
        reset();
        timers.writeTLR(0,load);timers.writeTCPR(0,compare);
        timers.writeTCSR(0,control);
        unsigned counter=0,flags=0;
        bool pending=true;
        unsigned random=12345;
        for(unsigned poll=0;poll<40;++poll)
        {
            random=random*1664525u+1013904223u;
            const auto count=1+(random%1024);
            for(unsigned tick=0;tick<count;++tick)
            {
                if(pending) {counter=load;pending=false;}
                else if(counter==0xffffff)
                {
                    flags|=1u<<20;
                    counter=control==0x271?load:0;
                }
                else ++counter;
                if(counter==compare)
                {
                    flags|=tcf;
                    if(control==0x201) pending=true;
                }
            }
            ticks(count);
            if(timers.readTCR(0)!=counter || (timers.readTCSR(0)&(3u<<20))!=flags)
            {
                std::cout<<"MISMATCH reference control="<<control<<" load="<<load
                         <<" compare="<<compare<<" poll="<<poll<<'\n';
                ++failures;break;
            }
            timers.writeTCSR(0,control|(3u<<20));flags=0;
        }
    }
    // Compare PWM pin edges with tick-by-tick reference, including inverted
    // polarity and delayed polls crossing multiple periods.
    for(const unsigned invert:{0u,0x100u})
    {
        reset();timers.writeTLR(0,0xffffab);timers.writeTCPR(0,0xffffd4);
        timers.writeTCSR(0,0xa71|invert);
        check("PWM initial edge deadline",timers.ticksUntilOutputEdge(0),42);
        auto before=timers.output(0);
        bool level=bool(invert),pending=true;unsigned counter=0;
        uint64_t rising=0,falling=0;
        for(unsigned poll=0;poll<100;++poll)
        {
            const unsigned count=1+(poll*137)%1024;
            for(unsigned i=0;i<count;++i)
            {
                unsigned edges=0;
                if(pending) {counter=0xffffab;pending=false;}
                else if(counter==0xffffff) {counter=0xffffab;++edges;}
                else ++counter;
                if(counter==0xffffd4) ++edges;
                while(edges--) {level=!level;level?++rising:++falling;}
            }
            ticks(count);auto output=timers.output(0);
            if(output.level!=level || output.risingEdges-before.risingEdges!=rising || output.fallingEdges-before.fallingEdges!=falling) {++failures;break;}
        }
    }
    std::cout<<"Timer audit mismatches="<<failures<<'\n';
    return failures?1:0;
}
