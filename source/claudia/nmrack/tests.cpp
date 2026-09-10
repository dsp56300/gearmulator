#include "seriallink.h"
#include "audioqueue.h"
#include "dac.h"
#include "playback.h"
#include "routing.h"
#include "jitconfig.h"
#include "dsp56kEmu/dsp.h"
#include <iostream>
#include <memory>

static void require(bool v,const char* message) {if(!v) throw std::runtime_error(message);}
int main()
{
    try
    {
        {
            nmrack::Commands commands;nmrack::Command event;unsigned grant=128;
            require(commands.push({nmrack::Command::Midi,100,0x90,60,100,0}),"Queue accepts note");
            require(commands.push({nmrack::Command::Midi,50,0x80,60,0,0}),"Queue accepts out-of-order timestamp");
            require(!commands.next(0,0,event,grant)&&grant==50,"Grant stops at earliest command");
            require(commands.next(50,0,event,grant)&&event.a==0x80,"Commands sorted by timestamp");
            grant=128;require(!commands.next(100,143,event,grant)&&grant==43,"MIDI serial wire-rate throttle");
            require(commands.next(143,143,event,grant)&&event.a==0x90,"Throttled command delivered");
            for(unsigned i=0;i<nmrack::Commands::Capacity;++i)
                require(commands.push({nmrack::Command::Parameter,100,0,1,uint16_t(i),0}),"Bounded inbox capacity");
            require(!commands.push({}),"Inbox overflow is explicit");
            for(unsigned i=0;i<nmrack::Commands::Capacity;++i)
                require(commands.next(100,0,event,grant)&&event.c==i,"Equal timestamp commands preserve submission order");
            commands.push({});require(commands.clear()&&!commands.next(100,0,event,grant),"Panic discards pending notes");
            auto producer=[&](uint16_t id)
            {for(uint16_t i=0;i<128;++i) if(!commands.push({nmrack::Command::Parameter,100,id,1,i,0})) std::terminate();};
            std::thread first(producer,0),second(producer,1);first.join();second.join();
            std::array<unsigned,2> next{};
            for(unsigned i=0;i<256;++i)
            {require(commands.next(100,0,event,grant),"Both control producers retained");require(event.a<2&&event.c==next[event.a]++,"Per-producer command ordering");}
            require(next[0]==128&&next[1]==128,"No lost or duplicated multi-producer commands");
        }
        const nmrack::AudioFrame routed{0.25f,0.5f,-0.25f,-1.f};
        for(unsigned ch=0;ch<4;++ch)
            require(nmrack::monitorSample(routed,ch,nmrack::Monitor::Four,0.5f)==routed[ch]*0.5f,"Four-channel routing and gain");
        for(unsigned ch=0;ch<2;++ch)
        {
            require(nmrack::monitorSample(routed,ch,nmrack::Monitor::Pair12,1)==routed[ch],"First monitor pair");
            require(nmrack::monitorSample(routed,ch,nmrack::Monitor::Pair34,1)==routed[ch+2],"Second monitor pair");
            require(nmrack::monitorSample(routed,ch,nmrack::Monitor::Mix,1)==(routed[ch]+routed[ch+2])*0.5f,"Stereo fold-down headroom");
        }
        // Conversion must retain input across arbitrary host block boundaries.
        for(double rate:{44100.,48000.,96000.,192000.})
        {
            auto render=[&](unsigned block)
            {
                nmrack::RateAdapter adapter(rate);uint64_t phase=0;
                std::vector<nmrack::AudioFrame> result(8192);
                for(unsigned offset=0;offset<result.size();)
                {
                    const auto n=std::min(block,unsigned(result.size())-offset);
                    adapter.render(result.data()+offset,n,[&](nmrack::AudioFrame* out,unsigned count)
                    {
                        for(unsigned i=0;i<count;++i,++phase)
                            out[i]={float(std::sin(phase*6.283185307179586*1000/96000)),0,0.25f,0};
                    });offset+=n;
                }
                return result;
            };
            const auto regular=render(128),irregular=render(7);
            double energy=0;
            for(unsigned i=512;i<regular.size();++i)
            {
                for(unsigned ch=0;ch<4;++ch)
                    require(std::abs(regular[i][ch]-irregular[i][ch])<1e-6,"Resampler block invariance");
                require(regular[i][1]==0&&regular[i][3]==0,"Resampler channel isolation");
                require(std::abs(regular[i][2]-0.25f)<0.001,"Resampler DC gain");
                energy+=regular[i][0]*regular[i][0];
            }
            require(energy/(regular.size()-512)>0.49&&energy/(regular.size()-512)<0.51,"Resampler passband gain");
        }
        {
            nmrack::RateAdapter adapter(48000);uint64_t phase=0;
            std::array<nmrack::AudioFrame,4096> output;
            adapter.render(output.data(),unsigned(output.size()),[&](nmrack::AudioFrame* out,unsigned count)
            {for(unsigned i=0;i<count;++i,++phase) out[i]={float(std::sin(phase*6.283185307179586*30000/96000)),0,0,0};});
            double energy=0;for(unsigned i=512;i<output.size();++i) energy+=output[i][0]*output[i][0];
            require(energy/(output.size()-512)<1e-6,"Downsampling rejects above-Nyquist tone");
        }
        for(bool linked:{false,true})
        {
            const auto config=nmrack::configureJit({},linked,true);
            require(config.maxInstructionsPerBlock==16,"Audio arithmetic keeps normal JIT granularity");
            for(auto pc:{0x9eu,0xcau,0xcfu,0x169u,0x175u,0x195u,0x19cu})
            {
                const auto precise=config.getBlockConfig(pc);
                require(precise&&precise->maxInstructionsPerBlock==1,"Timing-critical code is instruction precise");
                require(precise->linkInstructionLimitBlocks==linked&&precise->linkedBlockDeadlineChecks==linked,"Precise native links keep deadline checks");
            }
            for(auto pc:{0x9du,0x16au,0x16cu,0x174u,0x19du,0x550u})
                require(!config.getBlockConfig(pc),"Precision override is confined to control code");
            require(!nmrack::configureJit({},linked,false).getBlockConfig,"Legacy frame path retains its configuration");
        }
        nmrack::DacOutput dac;
        nmrack::AudioFrame converted{};
        require(nmrack::DacOutput::decode(0)==0&&nmrack::DacOutput::decode(0x20000)==-1&&nmrack::DacOutput::decode(0x3ffff)==-1.f/131072,"Signed 18-bit DAC boundaries");
        for(unsigned frame=0;frame<3;++frame)
        {
            const unsigned bank=0x6c0+32*(frame%2);
            unsigned edge=0;
            for(unsigned channel:{0u,2u,1u,3u})
            {
                const auto ready=dac.word(channel/2,bank+channel,100*(channel+1),frame+edge++*.1,converted);
                require(ready==(channel==3),"DAC waits for both complete output pairs");
            }
            require(converted[0]==200.f/131072&&converted[1]==100.f/131072&&converted[2]==400.f/131072&&converted[3]==300.f/131072,"Firmware-derived four-output mapping");
        }
        bool invalid=false;try {dac.word(0,0x6c0,0,4,converted);} catch(const std::runtime_error&) {invalid=true;}
        require(invalid,"DAC rejects repeated bank");
        nmrack::DacOutput incomplete;
        incomplete.word(0,0x6c0,0,0,converted);incomplete.word(0,0x6c1,0,.1,converted);
        incomplete.word(1,0x6c2,0,.2,converted);incomplete.word(1,0x6c3,0,.3,converted);
        incomplete.word(0,0x6e0,0,1,converted);
        invalid=false;try {incomplete.word(0,0x6c0,0,2,converted);} catch(const std::runtime_error&) {invalid=true;}
        require(invalid,"DAC cannot silently splice incomplete frames");

        nmrack::PlaybackQueue playback;
        std::array<nmrack::AudioFrame,7> callback;
        callback.fill({1,1,1,1});require(playback.consume(callback.data(),7)==0,"Empty playback callback is nonblocking");
        require(callback[0]==nmrack::AudioFrame{}&&playback.missingFrames()==7,"Underrun returns silence and counts shortage");
        std::atomic<bool> done{false};
        std::thread producer([&]{
            for(unsigned i=0;i<100000;++i)
            {const nmrack::AudioFrame frame{float(i),1,2,3};while(!playback.push(&frame,1)) std::this_thread::yield();}
            done.store(true,std::memory_order_release);
        });
        unsigned received=0;bool ordered=true;
        while(received<100000)
        {
            const auto n=playback.consume(callback.data(),unsigned(callback.size()));
            for(unsigned j=0;j<n;++j) ordered &= callback[j][0]==float(received++);
            if(!n) std::this_thread::yield();
        }
        producer.join();require(ordered&&done.load(),"Lock-free callback queue preserves order through wraparound");
        nmrack::AudioQueue audio;
        for(unsigned i=0;i<audio.Capacity;++i) audio.push({float(i),1,2,3});
        bool full=false;try {audio.push({});} catch(const std::runtime_error&) {full=true;}
        require(full,"Bounded audio production");
        std::array<nmrack::AudioFrame,7> output;
        unsigned read=0;
        while(auto n=audio.pop(output.data(),7))
            for(unsigned j=0;j<n;++j) require(output[j][0]==float(read++),"Audio target preserves frame order across chunks");
        require(read==audio.Capacity&&audio.size()==0,"Exact audio delivery");
        nmrack::SerialLink wire;
        nmrack::SerialLink::Packet packet{};
        wire.push({10,{0x123456,0xabcdef}},true);
        require(!wire.pop(9,packet),"No future serial data");
        require(wire.pop(10,packet)&&packet.words[0]==0x123456&&packet.words[1]==0xabcdef,"Ordered serial delivery");
        require(!wire.pop(10,packet),"No repeated packet");
        wire.push({11,{1,2}},true);wire.push({12,{3,4}},false);
        require(!wire.pop(13,packet)&&wire.disabled==1,"Disabled receiver does not accumulate a backlog");
        for(unsigned i=0;i<wire.Capacity;++i) wire.push({double(20+i),{i,i}},true);
        bool overflow=false;try {wire.push({100,{0,0}},true);} catch(const std::runtime_error&) {overflow=true;}
        require(overflow,"Overflow is explicit");

        dsp56k::DefaultMemoryValidator validator;
        auto periph=std::make_unique<dsp56k::Peripherals56303>();
        dsp56k::PeripheralsNop nop;
        dsp56k::Memory memory(validator,0x1000,0x10000,0x10000);
        dsp56k::DSP dsp(memory,periph.get(),&nop);
        auto& dma=periph->getDMA();auto& rx=periph->getEssi0();
        periph->getEssiClock().setEnabled(false);
        rx.writeCRB(0x20000);
        for(unsigned dor=0;dor<4;++dor)
        {
            dma.setDCR(2,0);dma.setDOR(dor,0xfffff8);
            dma.setDSR(2,0xffffb8);dma.setDDR(2,0x200);dma.setDCO(2,8);
            dma.setDCR(2,0xa85040|(dor<<7));
            for(unsigned i=0;i<27;++i)
            {
                rx.writeRX(0x123400+i);dma.trigger(dsp56k::DmaChannel::RequestSource::Essi0ReceiveData);
                require(memory.get(dsp56k::MemArea_X,0x200+i%9)==0x123400+i,"Circular RX DMA word");
                require(dma.getDDR(2)==0x200+(i+1)%9,"RX destination wraps every nine words");
                require(dma.getDSR(2)==0xffffb8&&(dma.getDCR(2)&0x800000),"Continuous RX retains source and enable");
            }
        }
        dma.setDCR(2,0);dma.setDOR(2,0xfffff8);dma.setDSR(2,0xffffb8);dma.setDDR(2,0x300);dma.setDCO(2,8);
        dma.setDCR(2,0x885140); // word-request, clear DE on block completion
        for(unsigned i=0;i<9;++i) dma.trigger(dsp56k::DmaChannel::RequestSource::Essi0ReceiveData);
        require(!(dma.getDCR(2)&0x800000)&&dma.getDDR(2)==0x300,"Single-block RX clears enable");
        dma.setDCR(2,0);dma.setDOR(2,1);dma.setDSR(2,0xffffb8);dma.setDDR(2,0x400);dma.setDCO(2,0x1002);
        dma.setDCR(2,0x885140);
        for(unsigned i=0;i<6;++i)
        {
            rx.writeRX(100+i);dma.trigger(dsp56k::DmaChannel::RequestSource::Essi0ReceiveData);
            require(memory.get(dsp56k::MemArea_X,0x400+i)==100+i,"Two-dimensional RX spans both lines");
            require(bool(dma.getDCR(2)&0x800000)==(i<5),"RX block completes only after its last line");
        }
        dma.setDCR(2,0);dma.setDOR(2,0xfffff8);dma.setDSR(2,0xffffb8);dma.setDDR(2,0x500);dma.setDCO(2,8);
        dma.setDCR(2,0xa85140);
        rx.writeCRA(0x1000); // two words per frame
        rx.writeCRB(0x22000);rx.writeRSMA(3);
        rx.setExternalReceiveClock(true);
        dsp56k::Audio::RxSlot input{};input[0]=0xabcdef;
        require(!rx.receiveWord(input,false)&&dma.getDDR(2)==0x500,"External RX waits for sync");
        rx.execRX();require(dma.getDDR(2)==0x500,"Internal clock cannot duplicate external RX");
        for(unsigned i=0;i<27;++i)
        {
            input[0]=0x234500+i;
            require(rx.receiveWord(input,i%2==0),"Externally clocked RX word accepted");
            require(memory.get(dsp56k::MemArea_X,0x500+i%9)==input[0],"RX word latched before immediate DMA");
            require(dma.getDDR(2)==0x500+(i+1)%9,"Word edges wrap nine-word DMA across frame boundaries");
        }
        rx.writeCRB(0);require(!rx.receiveWord(input,true),"Disabled RX drops external word");
        rx.writeCRB(0x22000);
        require(!rx.receiveWord(input,false),"Re-enabled RX waits for fresh sync");
        rx.writeRSMA(1);
        require(rx.receiveWord(input,true),"Unmasked first slot accepted");
        const auto destination=dma.getDDR(2);
        require(!rx.receiveWord(input,false)&&dma.getDDR(2)==destination,"Masked RX does not trigger DMA");
        require(!rx.receiveWord(input,false),"Completed external frame waits for next sync");
        rx.writeCRB(0);dma.setDCR(2,0);

        bool clockEnabled=false;
        rx.setClockGate([&]{return clockEnabled;});
        rx.writeCRB(0x22000);
        require(!rx.receiveWord(input,true),"All-GPIO gate blocks external RX");
        clockEnabled=true;
        require(!rx.receiveWord(input,false)&&rx.receiveWord(input,true),"Gate release waits for frame sync");
        rx.writeCRB(0);
        rx.writeCRA(0x2000);rx.writeRSMA(7);rx.writeCRB(0x22000);
        require(rx.receiveWord(input,true)&&rx.receiveWord(input,true)&&rx.receiveWord(input,false),"Early FS does not restart an in-progress frame");
        require(!rx.receiveWord(input,false)&&rx.receiveWord(input,true),"Frame gaps require a new FS edge");
        rx.writeCRB(0);rx.writeCRA(0x1000);

        // TX observation must see the outgoing word, not DMA's next word.
        rx.writeTSMA(1);rx.writeTX(0,0x112233);
        memory.set(dsp56k::MemArea_Y,0x600,0x445566);
        memory.set(dsp56k::MemArea_Y,0x601,0x445566);
        dma.setDSR(4,0x600);dma.setDDR(4,0xffffbc);dma.setDCO(4,1);
        dma.setDCR(4,0x885a51);
        rx.writeTX(0,0x112233); // DE may preload TX while TDE is already set.
        unsigned observed=0;bool sawMasked=false;
        rx.setWriteTxWordCallback([&](const dsp56k::Audio::TxSlot& word,uint32_t slot,bool active){
            if(active) {require(slot==0&&word[0]==0x112233,"TX callback precedes DMA refill");++observed;}
            else {require(slot==1&&word[0]==0,"Masked TX is explicitly undriven");sawMasked=true;}
        });
        clockEnabled=false;
        rx.writeCRB(0x12000);
        require(observed==0,"All-GPIO gate blocks TX including TE startup");
        clockEnabled=true;rx.execTX();
        require(observed==1&&rx.readTX(0)==0x445566,"TX request refills after observation");
        rx.execTX();require(sawMasked,"Masked TX still emits clock/slot event");
        rx.setWriteTxWordCallback({});rx.writeCRB(0);
        std::cout<<"PASS nmrack: serial word/frame edges, audio queue and circular peripheral RX DMA\n";
        return 0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
