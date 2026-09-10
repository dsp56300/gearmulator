// nmrack: experimental Rack emulator, currently at bounded firmware bring-up.
#include "hardware.h"
#include "nmmLib/nmmrom.h"
#include "nmmLib/nmmflash.h"
#include "nmmLib/nmmpcport.h"
#include "mc68k/mc68k.h"
#include "mc68k/hdi08.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspBootCode.h"
#include "seriallink.h"
#include "audioqueue.h"
#include "dac.h"
#include "jitconfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace nmrack
{
constexpr unsigned Count=4;
constexpr double SampleRate=96000; // Provisional codec rate; see investigation report.
std::vector<uint8_t> readRom(const std::string& path)
{
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot read Rack ROM");
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});
    auto hash=nmm::sha256(b);
    if(hash=="14eb3479fa8815da967bf636915d46a9a125d137b357edb0e2eae3d34f6ebdb3")
        for(size_t i=0;i<b.size();++i) b[i]=(((b[i]&85)<<1)|((b[i]&170)>>1))^uint8_t(17*(i+1));
    if(nmm::sha256(b)!="d9b199f27f573fdf7cd985fbd33cb9e60893a08912ab5d1bb562c77c663c997e")
        throw std::runtime_error("Unsupported Rack ROM hash");
    return b;
}
class Cpu final:public mc68k::Mc68k
{
public:
    explicit Cpu(const std::vector<uint8_t>& rom):ram(0x200000,0)
    {
        std::copy(rom.begin(),rom.end(),ram.begin());
        std::copy_n(rom.begin()+0xc800,0x5dfe0,ram.begin()+0x100000);
        reset(); setPC(0x100000);
    }
    uint32_t getResetPC() override {return 0x100000;}
    uint32_t getResetSP() override {return 0x200000;}
    void refreshInterrupts() {raiseIPL();}
    uint16_t readImm16(uint32_t a) override
    {if(a+1>=ram.size()) fail(a,"fetch");return uint16_t(ram[a])<<8|ram[a+1];}
    bool internal(uint32_t a)
    {
        auto p=static_cast<mc68k::PeriphAddress>(a&mc68k::g_peripheralMask);
        return a>=0xfff000&&(getSim().isInRange(p)||getGPT().isInRange(p)||getQSM().isInRange(p));
    }
    uint8_t read8(uint32_t a) override
    {
        if(a==0xfff907&&getGPT().getPortGP().getDirection()==0) return pcPort.data();
        if(a<ram.size()) return ram[a];
        if(a>=0x200000&&a<0x200040)
        {
            unsigned i=(a-0x200000)/8;
            if(i>=Count) return 0; // absent expansion board: HF2 low
            if(sync) sync(i);
            return host[i].read8(static_cast<mc68k::PeriphAddress>(a&7));
        }
        if(a>=0x300000&&a<0x400000) return flash.read(a-0x300000);
        if(a>=0x202000&&a<0x202008) return panel[a&7];
        if(a==0x201800) return 0xff; // panel buttons released
        if(a==0x202800) return 0x80; // panel analog multiplexer: knobs at midpoint
        if(internal(a)) return Mc68k::read8(a);
        fail(a,"read8");return 0;
    }
    uint16_t read16(uint32_t a) override
    {
        if(internal(a)) return Mc68k::read16(a);
        return uint16_t(read8(a))<<8|read8(a+1);
    }
    void write8(uint32_t a,uint8_t v) override
    {
        if(a<ram.size()) {ram[a]=v;return;}
        if(a>=0x200000&&a<0x200040)
        {
            unsigned i=(a-0x200000)/8;
            if(i>=Count) return;
            if(sync) sync(i);
            host[i].write8(static_cast<mc68k::PeriphAddress>(a&7),v);return;
        }
        if(a>=0x300000&&a<0x400000) {flash.write(a-0x300000,v);return;}
        if(a>=0x202000&&a<0x202008) {panel[a&7]=v;return;}
        if(a==0x201800) return;
        if(a==0xfffa11) pcPort.bus(v,getGPT().getPortGP().read());
        if(a==0xfffc15&&qsWrite) qsWrite(v);
        if(internal(a)) {Mc68k::write8(a,v);return;}
        fail(a,"write8");
    }
    void write16(uint32_t a,uint16_t v) override
    {
        if(a==0x201000) {keyScan=v;return;}
        if(internal(a)) {Mc68k::write16(a,v);return;}
        write8(a,v>>8);write8(a+1,v);
    }
    [[noreturn]] void fail(uint32_t a,const char* op)
    {std::ostringstream s;s<<op<<" address="<<std::hex<<a<<" MCU="<<getPC();throw std::runtime_error(s.str());}
    std::vector<uint8_t> ram;
    std::array<mc68k::Hdi08,Count> host;
    std::array<uint8_t,8> panel{};
    uint16_t keyScan=0;
    nmm::Flash flash;
    nmm::PcPort pcPort;
    std::function<void(unsigned)> sync;
    std::function<void(uint8_t)> qsWrite;
};
}
#define MC68K_CLASS nmrack::Cpu
#include "mc68k/musashiEntry.h"

namespace nmrack
{
struct Dsp
{
    dsp56k::DefaultMemoryValidator validator;
    dsp56k::Peripherals56303 periph;
    dsp56k::PeripheralsNop nop;
    dsp56k::Memory memory{validator,0x20000,0x200000,0x200000};
    dsp56k::DSP core{memory,&periph,&nop};
    dsp56k::DspBoot loader{core};
    bool runnable=false;
    bool clockConfigured=false,executing=false;
    uint64_t blockStart=0,blockRate=12288000,nonzeroTx=0;
    double position=0;
    uint64_t words=0,commands=0,tx[2]{},rx[2]{};
    explicit Dsp(bool linked,bool wordSerial)
    {
        core.getJit().setConfig(configureJit(core.getJit().getConfig(),linked,wordSerial));
        periph.getHI08().setRXRateLimit(0);
        periph.getHI08().setTransmitDataAlwaysEmpty(false);
        periph.getEssiClock().setExternalClockFrequency(12288000);
        periph.getEssiClock().setClockSource(dsp56k::EsxiClock::ClockSource::Cycles);
        periph.getTimers().setDSP(&core,true);
        periph.getDMA().setUseCycles(true);
        periph.getTimers().setTimerUpdateInterval(432);
        // DSP56303UM 7.5.9/7.5.10: all slot-mask bits reset enabled.
        // The shared ESSI currently initializes them to zero; the Rack relies
        // on the hardware reset values for its first/middle transmitters.
        for(auto* essi:{&periph.getEssi0(),&periph.getEssi1()})
        {essi->writeTSMA(0xffff);essi->writeTSMB(0xffff);essi->writeRSMA(0xffff);essi->writeRSMB(0xffff);}
    }
};
struct Machine
{
    std::vector<uint8_t> rom;
    Cpu cpu;
    std::array<std::unique_ptr<Dsp>,Count> dsps;
    double cpuPosition=0,target=0;
    uint64_t steps=0,budget=0;
    bool trace=false,uploadReturned=false,tablesVerified=false;
    bool toneInitialized=false;
    uint64_t mainLoopVisits=0;
    bool experimentalChain=false;
    bool wordSerial=false;
    bool validateDac=false,boardDiagnostics=false,captureDac=false;
    DacOutput dac;
    AudioQueue dacQueue;
    uint64_t dacWordsChecked=0,dacHeldWords=0;
    struct WordStats {uint64_t sent=0,accepted=0,rejected=0;double maxReceiverLead=0;};
    std::array<std::array<WordStats,2>,Count-1> wordStats{};
    struct WordEvent {double time,receiverLead;unsigned dsp,port,slot,word,active,pcrc,pcrd,dcr,ddr,accepted;};
    std::vector<WordEvent> wordEvents;
    struct QsEvent {double time;uint32_t pc;uint8_t value;};
    std::vector<QsEvent> qsEvents;
    struct BoardEvent {double time;unsigned dsp,pc;std::array<uint32_t,6> registers;};
    std::array<std::array<uint32_t,4>,Count> lastBoardState{};
    std::vector<BoardEvent> boardEvents;
    std::array<std::array<SerialLink,2>,Count-1> links;
    struct SerialEvent {double time;unsigned dsp,port;std::array<uint32_t,2> words;};
    std::vector<SerialEvent> serialEvents;
    bool captureSerial=false;
    std::exception_ptr peripheralFailure;
    AudioQueue mixQueue;
    bool captureMix=false;
    uint64_t mixFrames=0;
    std::array<uint64_t,Count> sampleEdges{};
    explicit Machine(const std::string& path,bool tracing,bool linked=true,bool chain=false,bool words=false,bool validate=false,bool board=false):rom(readRom(path)),cpu(rom),trace(tracing),experimentalChain(chain||words||validate),wordSerial(words||validate),validateDac(validate),boardDiagnostics(board)
    {
        for(auto& d:dsps) d=std::make_unique<Dsp>(linked,wordSerial);
        cpu.getQSM().setSciRxCycleTiming(true);
        cpu.sync=[this](unsigned i){catchUp(i,cpuPosition);transfer(i);};
        if(boardDiagnostics) cpu.qsWrite=[this](uint8_t value){
            if(!qsEvents.empty()&&qsEvents.back().value==value) return;
            if(qsEvents.size()==4096) throw std::runtime_error("QS transition capture capacity exhausted");
            qsEvents.push_back({cpuPosition,cpu.getPC(),value});
        };
        for(unsigned i=0;i<Count;++i)
        {
            auto& h=cpu.host[i];
            h.setReadIsrCallback([this,i](uint8_t v){
                transfer(i);auto& d=*dsps[i];auto& hi=d.periph.getHI08();
                return uint8_t((v&~0x1e)|(hi.readControlRegister()&0x18)|(!d.runnable||!hi.hasRXData()?6:0));
            });
            h.setRxEmptyCallback([this,i](bool){transfer(i);});
            h.setInitHdi08Callback([this,i]{auto& h=cpu.host[i];h.icr(h.icr()&0x7f);});
            h.setWriteTxCallback([this,i](uint32_t w){
                auto& d=*dsps[i];++d.words;
                if(trace) std::cout<<"word "<<i<<' '<<std::hex<<cpu.getPC()<<' '<<w<<std::dec<<'\n';
                if(!d.runnable)
                {
                    if(d.loader.hdiWriteTX(w))
                    {
                        const uint32_t src=i==0?0x50e44:i==Count-1?0x519ec:0x51418;
                        for(unsigned p=0;p<0x175;++p)
                        {
                            uint32_t expected=0;for(unsigned k=0;k<4;++k) expected=(expected<<8)|rom[src+4*p+k];
                            if(d.memory.get(dsp56k::MemArea_P,p)!=expected) throw std::runtime_error("Resident mismatch");
                        }
                        for(unsigned p=0x175;p<0x205;++p)
                        {
                            const auto expected=p<0x200?0:romWord(0x51fc0+4*(p-0x200));
                            if(d.memory.get(dsp56k::MemArea_P,p)!=expected)
                                throw std::runtime_error("Resident padding/tail mismatch");
                        }
                        d.runnable=true;d.position=cpuPosition;
                        std::cout<<"DSP "<<i<<" resident verified, MCU="<<std::hex<<cpu.getPC()<<std::dec<<'\n';
                    }
                }
                else d.periph.getHI08().writeRX(&w,1);
            });
            h.setWriteIrqCallback([this,i](uint8_t v){++dsps[i]->commands;dsps[i]->core.injectExternalInterrupt(v);});
            for(unsigned port=0;port<2;++port)
            {
                auto& essi=port?dsps[i]->periph.getEssi1():dsps[i]->periph.getEssi0();
                if(wordSerial)
                {
                    // Only the documented all-GPIO reset condition is modeled.
                    // Routing ESSI0's clock to ESSI1/other chips is still unknown.
                    essi.setClockGate([this,i,port]{return (dsps[i]->periph.read(port?0xffffaf:0xffffbf,dsp56k::Nop)&0x3f)!=0;});
                    if(i) essi.setExternalReceiveClock(true);
                    essi.setWriteTxWordCallback([this,i,port](const dsp56k::Audio::TxSlot& word,uint32_t slot,bool active){
                        safePeripheral([&]{transferWord(i,port,word[0],slot,active);});
                    });
                }
                essi.setReadRxCallback([this,i,port](uint64_t& index,dsp56k::Audio::RxFrame& frame){
                    frame.resize(2);frame[0].fill(0);frame[1].fill(0);++index;++dsps[i]->rx[port];
                    safePeripheral([&]{
                    if(experimentalChain&&!wordSerial&&i)
                    {
                        const auto now=dspTime(i);
                        catchUp(i-1,now);
                        SerialLink::Packet packet;
                        if(links[i-1][port].pop(now,packet))
                            for(unsigned s=0;s<2;++s) frame[s][0]=packet.words[s];
                    }
                    });
                });
                essi.setWriteTxCallback([this,i,port](uint64_t& index,const dsp56k::Audio::TxFrame& frame){
                    ++index;++dsps[i]->tx[port];
                    safePeripheral([&]{
                    for(unsigned s=0;s<frame.size();++s) if(frame[s][0]) ++dsps[i]->nonzeroTx;
                    if(captureSerial&&frame.size()==2)
                    {
                        if(serialEvents.size()==524288) throw std::runtime_error("Serial capture capacity exhausted");
                        serialEvents.push_back({dspTime(i),i,port,{frame[0][0],frame[1][0]}});
                    }
                    if(experimentalChain&&!wordSerial&&i+1<Count&&frame.size()==2)
                    {
                        auto& receiver=port?dsps[i+1]->periph.getEssi1():dsps[i+1]->periph.getEssi0();
                        links[i][port].push({dspTime(i),{frame[0][0],frame[1][0]}},receiver.hasEnabledReceivers());
                    }
                    });
                });
            }
        }
    }
    void transferWord(unsigned i,unsigned port,uint32_t word,uint32_t slot,bool active)
    {
        const auto now=dspTime(i);
        if(i==Count-1&&captureDac&&active)
        {
            auto& d=*dsps[i];auto& essi=port?d.periph.getEssi1():d.periph.getEssi0();
            // Callback runs before TDE is set and before the next DMA refill.
            // A full TX register identifies new payload, including repeated
            // numerical values. Do not infer freshness from sample amplitude.
            if(!(essi.readSR()&(1u<<dsp56k::Essi::SSISR_TDE)))
            {
                const auto address=(d.periph.getDMA().getDSR(4+port)-1)&0xffffff;
                const auto bank=address&~0x1fu,channel=address&0x1fu;
                if((bank!=0x6c0&&bank!=0x6e0)||channel>=4||channel/2!=port)
                    throw std::runtime_error("Unexpected DAC DMA source address");
                if(validateDac)
                {
                    if(word!=d.memory.get(dsp56k::MemArea_Y,address)) throw std::runtime_error("DAC serial payload differs from DMA source");
                    ++dacWordsChecked;
                }
                AudioFrame output;
                if(dac.word(port,address,word,now,output)) dacQueue.push(output);
            }
            else ++dacHeldWords;
        }
        double lead=0;unsigned accepted=0,dcr=0,ddr=0;
        if(i+1<Count)
        {
            // Acyclic forward propagation. The receiving DSP retires to this
            // word edge before RX latches and triggers DMA; no frame FIFO.
            catchUp(i+1,now);
            auto& next=*dsps[i+1];
            auto& rx=port?next.periph.getEssi1():next.periph.getEssi0();
            auto& stats=wordStats[i][port];++stats.sent;
            lead=std::max(0.0,next.position-now);
            stats.maxReceiverLead=std::max(stats.maxReceiverLead,lead);
            dcr=next.periph.getDMA().getDCR(2+port);
            ddr=next.periph.getDMA().getDDR(2+port);
            dsp56k::Audio::RxSlot input{};input[0]=word;
            // Masked TX slots are electrically undriven. Zero is only the
            // diagnostic board's input assumption, not a measured pull state.
            accepted=rx.receiveWord(input,slot==0);
            if(accepted) ++stats.accepted;else ++stats.rejected;
        }
        if(captureSerial)
        {
            if(wordEvents.size()==1048576) throw std::runtime_error("Word capture capacity exhausted");
            auto& periph=dsps[i]->periph;
            wordEvents.push_back({now,lead,i,port,slot,word,unsigned(active),
                periph.read(0xffffbf,dsp56k::Nop),periph.read(0xffffaf,dsp56k::Nop),dcr,ddr,accepted});
        }
    }
    template<typename F> void safePeripheral(F&& operation) noexcept
    {
        if(peripheralFailure) return;
        // DSP peripheral dispatch is noexcept. Defer diagnostics/budget errors
        // to the owner boundary rather than terminating inside an ESSI callback.
        try {operation();} catch(...) {peripheralFailure=std::current_exception();}
    }
    void checkBudget() {if(++steps>budget) throw std::runtime_error("Execution budget exhausted");}
    void observeBoard(unsigned i)
    {
        auto& d=*dsps[i];auto& p=d.periph;auto& dma=p.getDMA();
        const std::array<uint32_t,4> state{p.read(0xffffbf,dsp56k::Nop),p.read(0xffffaf,dsp56k::Nop),dma.getDCR(2),dma.getDCR(3)};
        if(state==lastBoardState[i]) return;
        lastBoardState[i]=state;
        if(boardEvents.size()==4096) throw std::runtime_error("Board transition capture capacity exhausted");
        boardEvents.push_back({d.position,i,d.core.getPC().toWord(),{state[0],state[1],state[2],state[3],dma.getDDR(2),dma.getDDR(3)}});
    }
    uint32_t romWord(unsigned offset) const
    {
        if(offset>rom.size()-4) throw std::runtime_error("ROM word outside image");
        uint32_t value=0;for(unsigned k=0;k<4;++k) value=(value<<8)|rom[offset+k];
        return value;
    }
    void verifyTables()
    {
        struct Table {dsp56k::EMemArea area;unsigned address,count,source;};
        for(unsigned i=0;i<Count;++i)
        {
            catchUp(i,cpuPosition+1); // retire the last host command before inspecting
            auto& d=*dsps[i];
            if(!d.runnable) throw std::runtime_error("Missing DSP at upload completion");
            for(auto t:{Table{dsp56k::MemArea_X,0x640,0x80,0x1527ac},
                Table{dsp56k::MemArea_Y,0x640,0x80,0x1529ac},
                Table{dsp56k::MemArea_X,0x700,0x100,0x152eac},
                Table{dsp56k::MemArea_Y,0x700,0x80,0x1532ac},
                Table{dsp56k::MemArea_Y,0x780,0x80,0x152cac}})
                for(unsigned j=0;j<t.count;++j)
                    if(d.memory.get(t.area,t.address+j)!=romWord(t.source-0x100000+0xc800+4*j))
                        throw std::runtime_error("DSP "+std::to_string(i)+" initialization table mismatch");
            for(unsigned a=0x56;a<0x5e;++a)
                if(d.memory.get(dsp56k::MemArea_X,a)!=0x100000)
                    throw std::runtime_error("DSP initial gain mismatch");
        }
        if(cpu.ram[0x1ab91c]!=Count) throw std::runtime_error("Incorrect DSP count");
        tablesVerified=true;
        std::cout<<"All four DSP initialization tables verified\n";
    }
    void transfer(unsigned i)
    {
        auto& hi=dsps[i]->periph.getHI08();auto& h=cpu.host[i];
        hi.setHostFlags((h.icr()>>3)&1,(h.icr()>>4)&1);
        if(h.canReceiveData()&&hi.hasTX()) h.writeRx(hi.readTX());
    }
    double dspTime(unsigned i) const
    {
        const auto& d=*dsps[i];
        return d.position+(d.executing?double(d.core.getCycles()-d.blockStart)*SampleRate/d.blockRate:0);
    }
    void catchUp(unsigned i,double end)
    {
        auto& d=*dsps[i];if(!d.runnable) return;
        if(d.executing) throw std::runtime_error("Recursive DSP execution");
        // A receiver must not independently spend its scheduler grant before
        // its source has emitted the words in that interval. During forward
        // edge delivery the source is already executing, so do not recurse.
        if(wordSerial&&i&&!dsps[i-1]->executing) catchUp(i-1,end);
        const auto clamp=d.core.getCycles()+100000;
        while(d.position<end&&d.core.getCycles()<clamp)
        {
            checkBudget();transfer(i);
            const auto old=d.core.getCycles();
            const auto rate=std::max<uint64_t>(12288000,d.periph.getEssiClock().getSpeedInHz());
            if(experimentalChain&&!d.clockConfigured&&d.periph.getEssiClock().getPCTL()==0x3c001a)
            {
                // DSP56303UM table 7-3: PSR=1 bypasses /8; 24-bit words,
                // PM=1 => 96 cycles, PM=2 => 144 cycles. Separate RX/TX clocks.
                // The first ADC RX clock and physical FS phases are provisional.
                auto& clock=d.periph.getEssiClock();
                clock.setCyclesPerSample(48);
                for(auto* essi:{&d.periph.getEssi0(),&d.periph.getEssi1()})
                    clock.setEsaiDivider(essi,i==Count-1?2:1,i==0?8:1);
                d.clockConfigured=true;
            }
            // Checked native links must return for the next sample IRQ and the
            // scheduler grant. Keep peripheral polling at every block boundary.
            const auto deadline=std::min(end,std::floor(d.position)+1);
            d.core.jitCycleDeadline=old+std::max<uint64_t>(1,uint64_t(std::ceil((deadline-d.position)*rate/SampleRate)));
            if(d.core.getPC().toWord()>=d.memory.sizeP()) throw std::runtime_error("DSP fetch outside P memory");
            d.blockStart=old;d.blockRate=rate;d.executing=true;
            const auto mode=d.core.getProcessingMode();
            try
            {
                d.core.exec();
                // Like NMM, keep DMA/ESSI running inside the long sample ISR.
                // Otherwise DMA0 clears the mix bank after the graph writes it.
                d.core.execPeriph<dsp56k::Peripherals56303,dsp56k::PeripheralsNop>();
            }
            catch(...) {d.executing=false;throw;}
            d.executing=false;
            if(peripheralFailure) std::rethrow_exception(peripheralFailure);
            if(captureMix&&i==Count-1&&mode==dsp56k::DSP::LongInterrupt&&d.core.getProcessingMode()!=mode)
            {
                const auto bank=d.memory.get(dsp56k::MemArea_X,5);
                if(bank!=0x6c0&&bank!=0x6e0) throw std::runtime_error("Unexpected Rack mix bank");
                AudioFrame frame;
                for(unsigned channel=0;channel<4;++channel)
                {
                    const auto word=d.memory.get(dsp56k::MemArea_Y,bank+channel)&0x3ffff;
                    frame[channel]=float(int32_t(word^0x20000)-0x20000)/131072.0f;
                }
                mixQueue.push(frame);++mixFrames;
            }
            d.position+=double(d.core.getCycles()-old)*SampleRate/rate;
            if(boardDiagnostics) observeBoard(i); // block-boundary observation, not a pin/write timestamp
            // Bring-up approximation inherited from NMM: external IRQD at the
            // audio rate. Physical Rack timer/serial pin wiring is unverified.
            const auto edge=static_cast<uint64_t>(d.position);
            if(edge>sampleEdges[i])
            {
                sampleEdges[i]=edge;
                if(d.periph.read(0xffffff,dsp56k::Nop)&0x600) d.core.injectExternalInterrupt(0x16);
            }
        }
    }
    void advance(unsigned frames)
    {
        target+=frames;
        for(;;)
        {
            double lag=cpuPosition;int who=-1;
            for(unsigned i=0;i<Count;++i) if(dsps[i]->runnable&&dsps[i]->position<lag) {lag=dsps[i]->position;who=int(i);}
            if(lag>=target) break;
            const double end=std::min(target,lag+SampleRate*0.000030);
            if(who>=0) catchUp(unsigned(who),end);
            else do
            {
                stepCpu();
            } while(cpuPosition<end);
        }
    }
    void stepCpu()
    {
        checkBudget();
        if(cpu.getPC()==0x10977e&&!uploadReturned) {verifyTables();uploadReturned=true;}
        if(cpu.getPC()==0x100e6e) ++mainLoopVisits;
        const auto cycles=cpu.exec();
        cpuPosition+=double(cycles)*SampleRate/cpu.getSim().getSystemClockHz();
        cpu.pcPort.advance(cycles,cpu.getSim().getSystemClockHz());
        if(cpu.pcPort.interrupt()&&(cpu.read16(0xfff920)&0x20)&&cpu.pcPort.takeInterrupt())
            cpu.getGPT().injectInterrupt(0xa);
        for(auto& h:cpu.host) h.exec(cycles);
        if(cpu.getPC()==0x10986c) throw std::runtime_error("MCU exception handler");
    }
    uint32_t call(uint32_t address,std::initializer_list<uint16_t> args)
    {
        // Setup-only adapter. Let firmware perform allocations and compilation;
        // preserve the interrupted MCU context while all peripherals keep time.
        struct Restore
        {
            Cpu& cpu;mc68k::CpuState state;
            ~Restore() {*cpu.getCpuState()=state;cpu.refreshInterrupts();}
        } restore{cpu,*cpu.getCpuState()};
        const auto sp=cpu.getAReg(7)-4-2*uint32_t(args.size());
        cpu.write16(sp,0);cpu.write16(sp+2,0x800);
        auto a=sp+4;for(auto v:args) {cpu.write16(a,v);a+=2;}
        cpu.getCpuState()->dar[15]=sp;cpu.setPC(address);
        const auto end=cpuPosition+SampleRate*2;
        while(cpu.getPC()!=0x800)
        {
            if(cpuPosition>=end) throw std::runtime_error("Native Rack call timed out: "+std::to_string(address));
            stepCpu();
            for(unsigned i=0;i<Count;++i) catchUp(i,cpuPosition);
        }
        target=std::max(target,cpuPosition);
        if(trace) std::cout<<"Native call "<<std::hex<<address<<" returned "<<cpu.getDReg(0)<<std::dec<<'\n';
        return cpu.getDReg(0);
    }
    void initializeTone()
    {
        if(!tablesVerified||mainLoopVisits<2) throw std::runtime_error("Tone setup requires a completed boot");
        if(toneInitialized) throw std::runtime_error("Diagnostic tone is already initialized");
        call(0x1106e0,{0,0,1,7}); // common-area oscillator A
        call(0x1106e0,{0,0,2,5}); // common-area output
        const uint8_t cable[]{2,0,1,64,0,0};
        for(unsigned j=0;j<6;++j) cpu.write8(0x1f0000+j,cable[j]);
        call(0x1192d6,{0,0,0x1f,0});
        call(0x1107e8,{1,0,0,1,0,64});
        call(0x1107e8,{1,0,0,1,1,64});
        call(0x1107e8,{1,0,0,2,0,127});
        cpu.write8(0x1c3ab0,1); // slot A active, consulted by 0x122438
        call(0x11029e,{100,1});
        if(call(0x10b3da,{0,0})!=1) throw std::runtime_error("Rack firmware rejected tone patch");
        toneInitialized=true;
        std::cout<<"Minimal Rack tone patch compiled\n";
    }
    void loadPatch(const nmm::Patch& patch)
    {
        if(!tablesVerified||mainLoopVisits<2||toneInitialized) throw std::runtime_error("Patch loading requires a freshly booted Rack");
        // Reject unported features before mutating firmware state.
        if(!patch.morphs.empty()||std::any_of(patch.morphKeyboard.begin(),patch.morphKeyboard.end(),[](auto v){return v!=0;}))
            throw std::runtime_error("Rack morph mappings are not yet supported");
        for(const auto& m:patch.modules)
        {
            call(0x1106e0,{0,m.area,m.index,m.type});
            const auto entry=0x1ab988+(m.area?0x4c66:0x467a)+4*m.index;
            const auto record=(uint32_t(cpu.read16(entry))<<16)|cpu.read16(entry+2);
            if(record<0x160000||record+36>=cpu.ram.size()) throw std::runtime_error("Invalid firmware module record");
            cpu.write8(record+15,m.x);cpu.write8(record+16,m.y);
            for(unsigned i=0;i<16;++i) cpu.write8(record+19+i,i<m.name.size()?uint8_t(m.name[i]):0);
        }
        for(const auto& c:patch.custom)
        {
            const auto size=call(0x110a0e,{0,c.area,c.module});
            if(size!=c.data.size()) throw std::runtime_error("Custom module data differs from Rack schema");
            const auto address=call(0x110994,{0,c.area,c.module});
            for(unsigned i=0;i<size;++i) cpu.write8(address+i,c.data[i]);
        }
        for(const auto& c:patch.controllers) call(0x110bfc,{c.index,0,c.area,c.module,c.parameter});
        for(const auto& c:patch.cables)
        {
            for(unsigned i=0;i<6;++i) cpu.write8(0x1f0000+i,c.record[i]);
            call(0x1192d6,{0,c.area,0x1f,0});
        }
        for(const auto& p:patch.parameters) call(0x1107e8,{1,0,p.area,p.module,p.index,p.value});
        for(const auto& k:patch.knobs) if(k.index<23) call(0x1160a0,{k.index,0,k.area,k.module,k.parameter});
        for(uint16_t g=0;g<4;++g) call(0x1107e8,{1,0,2,1,g,patch.morphValues[g]});
        call(0x112ea4,{0,patch.keyRangeMin,patch.keyRangeMax});
        call(0x112ef8,{0,patch.velocityRangeMin,patch.velocityRangeMax});
        call(0x110e14,{0,patch.requestedVoices});
        call(0x112e5a,{0,patch.bendRange});
        call(0x112d8a,{0,patch.portamento});call(0x112dd2,{0,patch.portamentoTime});
        call(0x101e0e,{0,patch.octaveShift});call(0x112f20,{0,patch.areaSeparator});
        call(0x112d64,{0,1,patch.voiceRetrigger});call(0x112d64,{0,0,patch.commonRetrigger});
        for(unsigned i=0;i<16;++i) cpu.write8(0x1b13aa+i,i<patch.name.size()?uint8_t(patch.name[i]):0);
        cpu.write8(0x1c3ab0,1);call(0x11029e,{100,1});
        if(call(0x10b3da,{0,0})!=1) throw std::runtime_error("Rack firmware rejected patch");
        toneInitialized=true;
    }
    void sendMidi(uint8_t status,uint8_t d1,uint8_t d2)
    {
        if(status<0x80||status>=0xf0||d1>127||d2>127) throw std::invalid_argument("Invalid MIDI channel message");
        auto& qsm=cpu.getQSM();qsm.writeSciRX(status);qsm.writeSciRX(d1);
        if((status&0xf0)!=0xc0&&(status&0xf0)!=0xd0) qsm.writeSciRX(d2);
    }
    void setParameter(uint16_t area,uint16_t module,uint16_t parameter,uint8_t value)
    {
        if(!toneInitialized||area>2||module<1||module>127||parameter>127||value>127||
            (area==2&&(module!=1||parameter>3))) throw std::invalid_argument("Invalid Rack parameter");
        // Only inject native setters at the MCU event-loop boundary, never in
        // an interrupted compiler/editor operation. Keep generated DAC audio.
        const auto deadline=cpuPosition+960;
        while(cpu.getPC()!=0x100e6e)
        {
            if(cpuPosition>=deadline) throw std::runtime_error("Rack control safe-point deadline");
            stepCpu();for(unsigned i=0;i<Count;++i) catchUp(i,cpuPosition);
        }
        call(0x1107e8,{1,0,area,module,parameter,value});
        call(0x10d8d8,{0,area,module,parameter,value});
    }
    void renderDiagnosticMix(AudioFrame* out,unsigned count)
    {
        if(!count) return;
        if(captureDac) throw std::runtime_error("Cannot mix diagnostic and DAC render targets");
        if(!experimentalChain||!tablesVerified||mainLoopVisits<2)
            throw std::runtime_error("Diagnostic mix requires a booted experimental chain");
        // Worker-only output-buffer target: drain exactly what the caller asks
        // for and retain surplus frames. This taps the completed native mixer,
        // not the still-unverified DAC latch/serial pin topology.
        captureMix=true;
        const auto deadline=cpuPosition+double(count)*2+SampleRate;
        while(count)
        {
            const auto n=mixQueue.pop(out,count);out+=n;count-=n;
            if(!count) break;
            if(cpuPosition>=deadline) throw std::runtime_error("Rack mixer output deadline exhausted");
            advance(std::min(count,64u));
        }
    }
    void initializeOutputTones()
    {
        if(!tablesVerified||mainLoopVisits<2||toneInitialized) throw std::runtime_error("Output test requires a freshly booted machine");
        call(0x1106e0,{0,0,5,3}); // four-output module, firmware type 3
        for(uint16_t ch=0;ch<4;++ch)
        {
            call(0x1106e0,{0,0,uint16_t(ch+1),7});
            const uint8_t cable[]{5,uint8_t(ch),uint8_t(ch+1),64,0,0};
            for(unsigned j=0;j<6;++j) cpu.write8(0x1f0000+j,cable[j]);
            call(0x1192d6,{0,0,0x1f,0});
            call(0x1107e8,{1,0,0,uint16_t(ch+1),0,uint16_t(57+ch*4)});
            call(0x1107e8,{1,0,0,uint16_t(ch+1),1,64});
        }
        call(0x1107e8,{1,0,0,5,0,100});
        cpu.write8(0x1c3ab0,1);call(0x11029e,{100,1});
        if(call(0x10b3da,{0,0})!=1) throw std::runtime_error("Rack rejected four-output test patch");
        toneInitialized=true;
    }
    void renderAudio(AudioFrame* out,unsigned count)
    {
        if(!count) return;
        if(!wordSerial||!tablesVerified||mainLoopVisits<2) throw std::runtime_error("DAC render requires a booted word-serial chain");
        if(captureMix) throw std::runtime_error("Cannot mix diagnostic and DAC render targets");
        captureDac=true;
        const auto deadline=cpuPosition+double(count)*2+SampleRate;
        while(count)
        {
            const auto n=dacQueue.pop(out,count);out+=n;count-=n;
            if(!count) break;
            if(cpuPosition>=deadline) throw std::runtime_error("DAC output deadline exhausted");
            advance(std::min(count,64u));
        }
    }
    void writeMixWav(const std::string& path,unsigned frames,bool serial=false)
    {
        if(serial&&!wordSerial) throw std::runtime_error("DAC WAV requires word-serial mode");
        std::ofstream file(path,std::ios::binary);
        if(!file) throw std::runtime_error("Cannot open diagnostic WAV");
        auto le=[&](uint32_t v,unsigned bytes){for(unsigned k=0;k<bytes;++k) file.put(char(v>>(8*k)));};
        const auto bytes=frames*4u*4u;
        file.write("RIFF",4);le(36+bytes,4);file.write("WAVEfmt ",8);le(16,4);
        le(3,2);le(4,2);le(unsigned(SampleRate),4);le(unsigned(SampleRate)*16,4);le(16,2);le(32,2);
        file.write("data",4);le(bytes,4);
        std::array<AudioFrame,128> buffer;
        while(frames)
        {
            auto count=std::min(frames,128u);
            if(serial) renderAudio(buffer.data(),count);else renderDiagnosticMix(buffer.data(),count);
            for(unsigned n=0;n<count;++n) for(auto sample:buffer[n])
            {uint32_t bits;std::memcpy(&bits,&sample,4);le(bits,4);}
            frames-=count;
        }
        captureMix=false;mixQueue.clear();
        captureDac=false;dacQueue.clear();dac.resetCapture();
        if(!file) throw std::runtime_error("Diagnostic WAV write failed");
        std::cout<<(serial?"Serial transaction DAC WAV written: ":"Diagnostic mixer WAV written: ")<<path<<'\n';
    }
    void report()
    {
        std::cout<<"dac_frames="<<dac.frames<<" dac_words_checked="<<dacWordsChecked<<" held_words="<<dacHeldWords<<" max_pair_skew="<<dac.maxSkew<<'\n';
        std::cout<<"MCU="<<std::hex<<cpu.getPC()<<std::dec<<" frames="<<cpuPosition<<" target="<<target
                 <<" detected="<<unsigned(cpu.ram[0x1ab91c])<<" upload_returned="<<uploadReturned
                 <<" tables_verified="<<tablesVerified<<" main_loop_visits="<<mainLoopVisits<<" steps="<<steps<<'\n';
        for(unsigned i=0;i<Count;++i) {auto& d=*dsps[i];std::cout<<"DSP "<<i<<" pc="<<std::hex<<d.core.getPC().var<<std::dec
            <<" frames="<<d.position<<" words="<<d.words<<" commands="<<d.commands<<" serial="<<d.tx[0]<<','<<d.tx[1]
            <<" nonzero_tx="<<d.nonzeroTx<<" sample_vector="<<std::hex<<d.memory.get(dsp56k::MemArea_P,0x17)
            <<" dma="<<d.periph.getDMA().getDCR(2)<<','<<d.periph.getDMA().getDCR(3)<<','
            <<d.periph.getDMA().getDCR(4)<<','<<d.periph.getDMA().getDCR(5)<<std::dec<<'\n';}
        if(wordSerial) for(unsigned i=0;i<Count-1;++i) for(unsigned p=0;p<2;++p)
        {const auto& w=wordStats[i][p];std::cout<<"word_link "<<i<<"->"<<i+1<<" port="<<p<<" sent="<<w.sent
            <<" accepted="<<w.accepted<<" rejected="<<w.rejected<<" max_receiver_lead_frames="<<w.maxReceiverLead<<'\n';}
        if(experimentalChain&&!wordSerial) for(unsigned i=0;i<Count-1;++i) for(unsigned p=0;p<2;++p)
        {auto& l=links[i][p];std::cout<<"link "<<i<<"->"<<i+1<<" port="<<p<<" delivered="<<l.delivered
            <<" missing="<<l.missing<<" disabled="<<l.disabled<<" high_water="<<l.highWater<<'\n';}
    }
    void writeDiagnostics(const std::string& prefix)
    {
        for(unsigned i=0;i<Count;++i)
        {
            std::ofstream p(prefix+"-dsp"+std::to_string(i)+".dsp",std::ios::binary);
            std::ofstream xy(prefix+"-dsp"+std::to_string(i)+"-xy.csv");
            if(!p||!xy) throw std::runtime_error("Cannot open diagnostic output");
            xy<<"address,x,y\n";
            for(unsigned a=0;a<0x1000;++a)
            {
                auto& m=dsps[i]->memory;const auto w=m.get(dsp56k::MemArea_P,a);
                for(int k=2;k>=0;--k) p.put(char(w>>(k*8)));
                xy<<a<<','<<m.get(dsp56k::MemArea_X,a)<<','<<m.get(dsp56k::MemArea_Y,a)<<'\n';
            }
            if(!p||!xy) throw std::runtime_error("Diagnostic write failed");
        }
        std::ofstream out(prefix+"-serial.csv");
        if(!out) throw std::runtime_error("Cannot open serial capture");
        out.precision(15);out<<"machine_frame,dsp,port,slot0,slot1\n";
        for(const auto& e:serialEvents) out<<e.time<<','<<e.dsp<<','<<e.port<<','<<e.words[0]<<','<<e.words[1]<<'\n';
        if(!out) throw std::runtime_error("Serial capture write failed");
        std::ofstream words(prefix+"-words.csv"),qs(prefix+"-qs.csv"),board(prefix+"-board.csv");
        if(!words||!qs||!board) throw std::runtime_error("Cannot open word/GPIO capture");
        words.precision(15);words<<"machine_frame,receiver_lead,dsp,port,slot,word,active,pcrc,pcrd,rx_dcr,rx_ddr,accepted\n";
        for(const auto& e:wordEvents) words<<e.time<<','<<e.receiverLead<<','<<e.dsp<<','<<e.port<<','<<e.slot<<','<<e.word<<','<<e.active
            <<','<<e.pcrc<<','<<e.pcrd<<','<<e.dcr<<','<<e.ddr<<','<<e.accepted<<'\n';
        qs.precision(15);qs<<"machine_frame,mcu_pc,portqs\n";
        for(const auto& e:qsEvents) qs<<e.time<<','<<e.pc<<','<<unsigned(e.value)<<'\n';
        board.precision(15);board<<"machine_frame,dsp,pc,pcrc,pcrd,dcr2,dcr3,ddr2,ddr3\n";
        for(const auto& e:boardEvents) {board<<e.time<<','<<e.dsp<<','<<e.pc;for(auto r:e.registers) board<<','<<r;board<<'\n';}
        if(!words||!qs||!board) throw std::runtime_error("Word/GPIO capture write failed");
    }
};
    Hardware::Hardware(const std::string& firmware,Options options)
        :impl(std::make_unique<Machine>(firmware,options.trace,options.linkedJit,options.experimentalChain,options.wordSerial,options.validateDac,options.boardDiagnostics)) {}
    Hardware::~Hardware()=default;
    void Hardware::setStepBudget(uint64_t budget) {impl->budget=budget;}
    void Hardware::advance(unsigned frames)
    {
        if(frames&&(impl->captureDac||impl->captureMix)) throw std::runtime_error("Active audio target must be drained through its render API");
        while(frames) {const auto n=std::min(frames,128u);impl->advance(n);frames-=n;}
    }
    void Hardware::boot(unsigned frames)
    {
        advance(frames);
        if(!impl->tablesVerified||impl->mainLoopVisits<2)
            throw std::runtime_error("Rack boot did not reach a recurring application event loop");
    }
    void Hardware::initializeTone() {impl->initializeTone();}
    void Hardware::initializeOutputTones() {impl->initializeOutputTones();}
    void Hardware::loadPatch(const nmm::Patch& patch) {impl->loadPatch(patch);}
    void Hardware::sendMidi(uint8_t status,uint8_t d1,uint8_t d2) {impl->sendMidi(status,d1,d2);}
    void Hardware::setParameter(uint16_t area,uint16_t module,uint16_t parameter,uint8_t value) {impl->setParameter(area,module,parameter,value);}
    Hardware::Voices Hardware::voices() const
    {
        Voices result;const auto& ram=impl->cpu.ram;constexpr unsigned poly=0x1ab988+0x4c66;
        result.allocated=ram[poly+0x1fc];result.used=ram[poly+0x224];
        if(result.allocated>32) throw std::runtime_error("Invalid Rack voice allocation count");
        for(unsigned i=0;i<result.allocated;++i)
        {
            const auto binding=poly+0x228+14*i,voice=poly+0x3e8+14*i;
            result.voice[i]={ram[binding],ram[voice+9],ram[voice+10],ram[voice+12],uint16_t((ram[binding+6]<<8)|ram[binding+7])};
        }
        return result;
    }
    void Hardware::renderAudio(AudioFrame* output,unsigned frames)
    {if(frames&&!output) throw std::invalid_argument("Null audio output");impl->renderAudio(output,frames);}
    void Hardware::writeDacWav(const std::string& path,unsigned frames)
    {if(frames>0x0ffffff0) throw std::invalid_argument("WAV too large");impl->writeMixWav(path,frames,true);}
    void Hardware::renderDiagnosticMix(AudioFrame* output,unsigned frames)
    {if(frames&&!output) throw std::invalid_argument("Null audio output");impl->renderDiagnosticMix(output,frames);}
    void Hardware::writeMixWav(const std::string& path,unsigned frames)
    {if(frames>0x0ffffff0) throw std::invalid_argument("WAV too large");impl->writeMixWav(path,frames);}
    void Hardware::captureDiagnostics(const std::string& prefix)
    {
        impl->serialEvents.clear();impl->serialEvents.reserve(524288);impl->captureSerial=true;
        impl->wordEvents.clear();if(impl->wordSerial) impl->wordEvents.reserve(1048576);
        try {advance(9600);} catch(...) {impl->captureSerial=false;throw;}
        impl->captureSerial=false;impl->writeDiagnostics(prefix);
    }
    void Hardware::report() const {impl->report();}
    Hardware::Status Hardware::status() const
    {
        Status result{impl->tablesVerified,impl->mainLoopVisits,impl->steps,impl->mixFrames,impl->cpuPosition};
        result.dacFrames=impl->dac.frames;result.dacWordsChecked=impl->dacWordsChecked;result.dacHeldWords=impl->dacHeldWords;result.dacMaxSkew=impl->dac.maxSkew;
        for(const auto& pair:impl->wordStats) for(const auto& w:pair)
        {result.serialWords+=w.sent;result.acceptedSerialWords+=w.accepted;result.maxReceiverLeadFrames=std::max(result.maxReceiverLeadFrames,w.maxReceiverLead);}
        return result;
    }
}
