#include "nmmcapture.h"
#include "nmmaudioworker.h"
#include <cstring>
#include "nmmhardware.h"
#include "nmmrom.h"
#include "nmmpatch.h"
#include "nmmcodec.h"
#include "nmmflash.h"
#include "nmmpcport.h"
#include "nmmeditorstate.h"
#include "mc68k/mc68k.h"
#include "mc68k/hdi08.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspBootCode.h"
#include "dsp56kEmu/debuggerinterface.h"
#include "dsp56kEmu/jitblockruntimedata.h"
#include <functional>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace nmm
{
	class Microcontroller final : public mc68k::Mc68k
	{
	public:
		explicit Microcontroller(const Rom& rom) : ram(0x200000,0)
		{
			std::copy(rom.data().begin(),rom.data().end(),ram.begin()+Rom::Base);
			reset();
			setPC(Rom::Base);
		}
		uint32_t getResetPC() override { return Rom::Base; }
		uint32_t getResetSP() override { return 0x200000; }
		void refreshInterruptLevel() { raiseIPL(); }
		uint16_t readImm16(uint32_t addr) override { if((addr>=8 && addr<0x100000) || addr>=0x200000) fail("instruction-fetch",addr,0); return read16(addr); }
		bool internal(uint32_t addr)
		{
			const auto a=static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);
			return addr>=0xfff000 && (getSim().isInRange(a)||getGPT().isInRange(a)||getQSM().isInRange(a));
		}
		uint8_t read8(uint32_t addr) override
		{
			if(inspection && addr>=ram.size()) fail("snapshot peripheral read",addr,0);
            if(addr==0xfff907 && getGPT().getPortGP().getDirection()==0) return pcPort.data();
			if(addr<ram.size()) return ram[addr];
			if(addr>=0x300000 && addr<0x400000) return flash.read(addr-0x300000);
			if(addr>=0x200000 && addr<0x200008) { if(synchronizeHost) synchronizeHost();return host.read8(static_cast<mc68k::PeriphAddress>(addr-0x200000)); }
			if(internal(addr)) return Mc68k::read8(addr);
			fail("read8",addr,0); return 0;
		}
		uint16_t read16(uint32_t addr) override
		{
			if(inspection && addr+1>=ram.size()) fail("snapshot peripheral read",addr,0);
            if(addr+1<ram.size()) return (uint16_t(ram[addr])<<8)|ram[addr+1];
			if(addr>=0x300000 && addr<0x400000) return (uint16_t(read8(addr))<<8)|read8(addr+1);
			if(addr>=0x200000 && addr<0x200008) { if(synchronizeHost) synchronizeHost();return host.read16(static_cast<mc68k::PeriphAddress>(addr-0x200000)); }
			if(internal(addr)) return Mc68k::read16(addr);
			fail("read16",addr,0); return 0;
		}
		void write8(uint32_t addr,uint8_t v) override
		{
			if(inspection && addr>=ram.size()) fail("snapshot peripheral write",addr,v);
            if(addr==0xfffa11) pcPort.bus(v,getGPT().getPortGP().read());
			if(addr<ram.size()) { ram[addr]=v; return; }
			if(addr>=0x200000 && addr<0x200008) { if(synchronizeHost) synchronizeHost();host.write8(static_cast<mc68k::PeriphAddress>(addr-0x200000),v); return; }
			if(internal(addr)) { Mc68k::write8(addr,v); return; }
			if(addr==0x202000 || addr==0x201800) { panelLatch=v;return;}
			if(addr>=0x300000 && addr<0x400000) {flash.write(addr-0x300000,v);return;}
			fail("write8",addr,v);
		}
		void write16(uint32_t addr,uint16_t v) override
		{
			if(inspection && addr+1>=ram.size()) fail("snapshot peripheral write",addr,v);
            if(addr+1<ram.size()) {ram[addr]=v>>8; ram[addr+1]=v; return;}
			if(addr>=0x200000 && addr<0x200008) {if(synchronizeHost) synchronizeHost();host.write16(static_cast<mc68k::PeriphAddress>(addr-0x200000),v); return;}
			if(internal(addr)) { Mc68k::write16(addr,v); return; }
			fail("write16",addr,v);
		}
		void fail(const char* op,uint32_t addr,uint32_t value)
		{
			std::ostringstream out;
			out << "Unmapped CPU " << op << " PC=0x" << std::hex << getPC() << " address=0x" << addr << " value=0x" << value;
			throw std::runtime_error(out.str());
		}
		std::function<void()> synchronizeHost;
		bool inspection=false;
        uint8_t panelLatch=0;
		std::vector<uint8_t> ram;
        Flash flash;
		mc68k::Hdi08 host;
		PcPort pcPort;
	};
}
#define MC68K_CLASS nmm::Microcontroller
#include "mc68k/musashiEntry.h"

namespace nmm
{
	struct Hardware::Impl
	{
		std::shared_ptr<const Rom> firmware;
        const Rom& rom;
        const std::atomic<unsigned>* cancelGeneration=nullptr;
        const std::atomic<bool>* cancelStop=nullptr;
        unsigned expectedGeneration=0;
		Microcontroller cpu;
		dsp56k::DefaultMemoryValidator validator;
		dsp56k::Peripherals56303 peripherals;
		dsp56k::PeripheralsNop nop;
		dsp56k::Memory memory;
		dsp56k::DSP dsp;
		dsp56k::DspBoot loader;
		std::ostream* trace;
		std::vector<uint32_t> bootWords;
		uint64_t steps=0, frames=0, commands=0, lastMainLoop=0;
		bool uploaded=false, tables=false, booted=false, patchLoaded=false;
		double dspCredit=0;
        // One 96 kHz frame of MCU credit; HDI accesses flush sooner. Local DSP
        // deadlines remain serviced at every JIT block boundary inside the burst.
        uint32_t burstFrames=1;
        uint32_t audioQuantum=0;
        bool audioEpoch=false,audioPending=false,audioHostDone=false;
        double audioCpuAllowance=0;
        dsp56k::TWord audioHostFence=0;
        std::unique_ptr<AudioWorker> audioWorker;
        uint64_t burstCount=0,burstCycles=0,burstMaxCycles=0;
		bool capturing=false,captureOverflow=false,captureControlAudio=true;
        Capture* capture=nullptr;
        uint64_t compilations=0;bool compilationDiagnostics=false;
        bool deadlineGraphs=false;
        bool diagnostics=false;
        Hardware::Timing diagnostic{};
        struct DmaBoundary {uint64_t cycles,frame,clock;uint32_t pc,next,phase,dcr,ddr,pending,mixSelector,left,right,otherLeft,otherRight,tx4,tx5;};
        std::vector<DmaBoundary> dmaBoundaries;
        size_t dmaBoundaryCount=0;
        bool dmaBoundaryFull=false;
        void recordDmaBoundary(uint32_t pc,unsigned phase)
        {
            if(dmaBoundaries.empty()) return;
            if(dmaBoundaryCount==dmaBoundaries.size()) {dmaBoundaryFull=true;return;}
            const auto& channel=peripherals.getDMA().channel(0);
            const auto dest=memory.get(dsp56k::MemArea_X,5);
            dmaBoundaries[dmaBoundaryCount++]={dsp.getCycles(),frames/2,channel.lastTransferClock(),pc,dsp.getPC().toWord(),phase,
                channel.getDCR(),channel.getDDR(),uint32_t(channel.pendingTransferClocks()),dest,
                memory.get(dsp56k::MemArea_Y,0x6c0),memory.get(dsp56k::MemArea_Y,0x6c1),
                memory.get(dsp56k::MemArea_Y,0x6e0),memory.get(dsp56k::MemArea_Y,0x6e1),
                peripherals.getDMA().getDCR(4),peripherals.getDMA().getDCR(5)};
        }
        std::ostream* coefficientTrace=nullptr;
        std::array<uint64_t,1024> irqCycles{};uint64_t irqRead=0,irqWrite=0;
		std::array<float,2> latest{};
		std::array<std::array<float,2>,256> audio{};
		uint64_t audioRead=0,audioWrite=0;
		std::array<std::array<float,2>,1024> inputQueue{};
        uint64_t inputRead=0,inputWrite=Hardware::InputBufferLatency;
		
		std::array<float,2> inputFrame{};
		std::array<double,12> serialSum{};
		std::array<double,12> serialSquares{};
		std::array<float,12> serialMin{}, serialMax{};
		std::array<uint64_t,12> serialCounts{};
		std::array<uint64_t,0x800> dspBlockEntries{};
		std::string milestone="firmware validated";
		uint32_t lastTracePc=0xffffffff;
		uint16_t lastMidiQueueCount=0xffff, lastMidiSecondaryCount=0xffff;
		uint32_t lastMidiWrite=0xffffffff, lastMidiSecondaryWrite=0xffffffff;

		Impl(std::shared_ptr<const Rom> image,std::ostream* output)
			: firmware(std::move(image)),rom(*firmware),cpu(rom),memory(validator,0x20000,0x200000,0x200000),dsp(memory,&peripherals,&nop),loader(dsp),trace(output)
		{
			auto config=dsp.getJit().getConfig();
            cpu.getQSM().setSciRxCycleTiming(true);
			config.maxInstructionsPerBlock=16;
			config.enableOptimizer=true;
			config.memoryWritesCallCpp=false;
			config.maxDoIterations=1;
			config.linkJitBlocks=false;
			config.dynamicPeripheralAddressing=true;
			config.dynamicFastInterrupts=true;
			dsp.getJit().setConfig(config);
			auto& hi=peripherals.getHI08();
			hi.setRXRateLimit(0);
			hi.setTransmitDataAlwaysEmpty(false);
			cpu.synchronizeHost=[this]{
                if(audioEpoch) {waitAudioDsp();return;}
                if(uploaded && burstFrames && capturing) runDsp();
            };
		cpu.host.isr(6);
			cpu.host.setRxEmptyCallback([this](bool){ transfer(); });
			cpu.host.setReadIsrCallback([this](uint8_t value)
			{
				transfer();
                if(captureOverflow) throw std::runtime_error("Audio capture ring overflow");
				auto& hi=peripherals.getHI08();
				return uint8_t((value & ~0x1e) | (hi.readControlRegister()&0x18) | ((!uploaded || !hi.hasRXData())?6:0));
			});
			cpu.host.setWriteTxCallback([this](uint32_t word)
			{
				if(capture) capture->push(dsp.getCycles(),frames/2,Capture::HostWord,{cpu.getPC(),word},cpu.getCycles());
				if(trace) *trace << "host-tx " << std::hex << cpu.getPC() << ' ' << word << '\n';
				if(!uploaded)
				{
					bootWords.push_back(word);
					const auto n=bootWords.size();
					if((n==1 && word!=0x205)||(n==2 && word!=0)) throw std::runtime_error("Unexpected DSP boot header");
					if(loader.hdiWriteTX(word))
					{
						auto expected=rom.resident();
						if(!std::equal(expected.begin(),expected.end(),bootWords.begin()+2)) throw std::runtime_error("DSP resident upload mismatch");
						uploaded=true; milestone="resident upload verified";
					}
				}
				else peripherals.getHI08().writeRX(&word,1);
			});
			cpu.host.setWriteIrqCallback([this](uint8_t vector)
			{
				++commands;
				if(trace) *trace << "host-irq " << std::hex << cpu.getPC() << ' ' << unsigned(vector) << '\n';
                if(audioEpoch) serviceAudioHostIrq(vector);
                else dsp.injectExternalInterrupt(vector);
			});
			cpu.host.setInitHdi08Callback([this]{cpu.host.icr(cpu.host.icr()&0x7f);cpu.host.isr(cpu.host.isr()|6);});
			for(unsigned port=0;port<2;++port)
			{
				auto* essi=port==0?&peripherals.getEssi0():&peripherals.getEssi1();
				essi->setReadRxCallback([this,port](uint64_t& index,dsp56k::Audio::RxFrame& f)
                {
                    f.resize(2);f[0].fill(0);f[1].fill(0);
                    // Firmware enables receive slot 0 on both ports. DMA3 latches
                    // ESSI1 at X:6c5; its interrupt reads ESSI0 at X:6c4.
                    f[0][0]=encodeAdc(inputFrame[1-port]);
                    ++index;
                });
				essi->setWriteTxCallback([this,port](uint64_t& index,const dsp56k::Audio::TxFrame& f)
				{
					++index;++frames;
					if(trace) for(unsigned slot=0;slot<f.size() && slot<2;++slot)
						for(unsigned ch=0;ch<3;++ch)
						{
							const auto value=dsp56k::dsp2sample<float>(f[slot][ch]);
							const auto channel=port*6+slot*3+ch;
							if(serialCounts[channel]==0) serialMin[channel]=serialMax[channel]=value;
							else {serialMin[channel]=std::min(serialMin[channel],value);serialMax[channel]=std::max(serialMax[channel],value);}
							serialSum[channel]+=value;serialSquares[channel]+=double(value)*value;
							++serialCounts[channel];
						}
					latest[1-port]=f.empty()?0:decodeDac(f[0][0]);
					// Provisional board wiring: the shared codec frame edge drives IRQD.
					if(port==1 && (peripherals.read(0xffffff,dsp56k::Nop) & 0x600))
                    {
                        if(diagnostics) {++diagnostic.irqRequests;irqCycles[irqWrite++%irqCycles.size()]=dsp.getCycles();diagnostic.maxPendingIrqs=std::max(diagnostic.maxPendingIrqs,irqWrite-irqRead);}
                        dsp.injectExternalInterrupt(0x16);
                    }
					if(capturing && trace && index%96000==0) { *trace << "serial " << port << " slots " << f.size();for(unsigned slot=0;slot<f.size();++slot) for(unsigned ch=0;ch<3;++ch) *trace << ' ' << std::hex << f[slot][ch];*trace << " highest_note=" << std::dec << unsigned(ram8(0x175f40+0x3490)) << " requested_voices=" << unsigned(ram8(0x175f40+0x5a1a)) << '\n';}
					if(port==1)
                    {
                        if(capturing)
                        {
                            if(audioWrite-audioRead==audio.size()) captureOverflow=true;
                            else audio[audioWrite++%audio.size()]=latest;
                        }
                        inputFrame=capturing && inputRead<inputWrite?inputQueue[inputRead++%inputQueue.size()]:std::array<float,2>{};
                    }
				});
			}
			// CRA=0x181808: PSR bypass, PM+1=9, 24 bits, two slots.
			// 96 kHz * 2 * 24 * 2 * 9 = 82.944 MHz core; PCTL=0x3c001a
			// multiplies EXTAL by 27/4, giving 12.288 MHz. Timer 0's
			// 432 CLK/2 ticks independently agree with this sample period.
			peripherals.getEssiClock().setSamplerate(96000);
			peripherals.getEssiClock().setExternalClockFrequency(12288000);
			peripherals.getEssiClock().setClockSource(dsp56k::EsxiClock::ClockSource::Cycles);
			peripherals.getTimers().setDSP(&dsp,true);
            peripherals.getDMA().setUseCycles(true);
			// Poll flags at each serial slot; arithmetic retains intervening PWM edges.
            peripherals.getTimers().setTimerUpdateInterval(432);
		}
        ~Impl() {audioWorker.reset();} // Join before any callback storage is destroyed.
		uint8_t ram8(uint32_t address) const
		{
			return address<cpu.ram.size()?cpu.ram[address]:0;
		}
		uint16_t ram16(uint32_t address) const
		{
			return uint16_t(ram8(address)<<8)|ram8(address+1);
		}
		uint32_t ram32(uint32_t address) const
		{
			return (uint32_t(ram8(address))<<24)|(uint32_t(ram8(address+1))<<16)|(uint32_t(ram8(address+2))<<8)|ram8(address+3);
		}
		void traceVoiceState(const char* reason)
		{
			if(!trace) return;
			*trace << "midi-voice " << reason << " state=" << std::hex << unsigned(ram8(0x165b44))
				<< " callback=" << ram32(0x17c084) << " active=";
			for(unsigned i=0;i<1;++i)
			{
				const auto base=0x175f40u+i*0x6000u;
				*trace << '[' << base << " en=" << unsigned(ram8(0x17c05f+i)) << " ch=" << unsigned(ram8(base+0x3494))
					<< " highest_note=" << unsigned(ram8(base+0x3490)) << " note_callbacks=" << unsigned(ram8(base+0x4672)) << " requested_voices=" << unsigned(ram8(base+0x5a1a))
					<< " lo=" << unsigned(ram8(base+0x5a1b)) << " hi=" << unsigned(ram8(base+0x5a1c))
					<< " vlo=" << unsigned(ram8(base+0x5a1d)) << " vhi=" << unsigned(ram8(base+0x5a1e)) << ':';
				for(unsigned j=0;j<8;++j) *trace << std::setw(2) << std::setfill('0') << unsigned(ram8(base+j));
				*trace << std::setfill(' ') << ']';
				for(unsigned area=0;area<2;++area)
				{
					const auto a=base+(area?0x4c66:0x467a);
					*trace << " area" << area << "={allocated=" << unsigned(ram8(a+0x1fc))
						<< " used=" << unsigned(ram8(a+0x224)) << " binding=";
					for(unsigned j=0;j<14;++j) *trace << std::setw(2) << std::setfill('0') << unsigned(ram8(a+0x228+j));
					*trace << std::setfill(' ') << '}';
				}
			}
			*trace << " dsp_pc=" << dsp.getPC().toWord() << " p17=" << memory.get(dsp56k::MemArea_P,0x17)
				<< " x1=" << memory.get(dsp56k::MemArea_X,1) << '\n';
		}
		void traceMidiQueues()
		{
			if(!trace) return;
			const auto eventCount=ram16(0x1659a4);
			const auto eventWrite=ram32(0x1659a0);
			const auto eventRead=ram32(0x16599c);
			const auto secondaryCount=ram16(0x165b42);
			const auto secondaryWrite=ram32(0x165b3e);
			if(eventCount!=lastMidiQueueCount || eventWrite!=lastMidiWrite)
			{
				*trace << "midi-queue primary pc=0x" << std::hex << cpu.getPC() << " count=" << std::dec << eventCount << " read=0x" << std::hex << eventRead << " write=0x" << eventWrite;
				if(eventWrite>=4 && eventWrite<=0x165998)
				{
					const auto record=eventWrite-4;
					*trace << " record=";
					for(unsigned i=0;i<4;++i) *trace << std::setw(2) << std::setfill('0') << unsigned(ram8(record+i));
					*trace << std::setfill(' ');
				}
				*trace << '\n';
				lastMidiQueueCount=eventCount; lastMidiWrite=eventWrite;
				traceVoiceState("primary-change");
			}
			if(secondaryCount!=lastMidiSecondaryCount || secondaryWrite!=lastMidiSecondaryWrite)
			{
				*trace << "midi-queue secondary count=" << std::dec << secondaryCount << " write=0x" << std::hex << secondaryWrite << '\n';
				lastMidiSecondaryCount=secondaryCount; lastMidiSecondaryWrite=secondaryWrite;
				traceVoiceState("secondary-change");
			}
		}
		void traceMidiEntry(uint32_t pc)
		{
            if(capture && (pc==0x107728 || pc==0x107bc4 || pc==0x107928 || pc==0x108e76))
                capture->push(dsp.getCycles(),frames/2,Capture::McuEntry,{pc,cpu.getDReg(0),cpu.getDReg(1),cpu.getDReg(2),cpu.getAReg(0)},cpu.getCycles());
			if(!trace) return;
			if(pc==0x10791e) traceVoiceState("note-on-return");
			if(pc==0x10eb14 && ram16(0x1659a4)==0 && ram16(0x165b42)==0) return;
			if(pc==0x101a9e || pc==0x101abc || pc==0x10eb14 || pc==0x10ee72 || pc==0x10ef62 || pc==0x10efb8 || pc==0x10efc0 || pc==0x11aabe || pc==0x11ab48 ||
				pc==0x10eb7a || pc==0x10eb98 || pc==0x10eba8 || pc==0x10ebb8 || pc==0x10ebc8 || pc==0x10ebd8 || pc==0x10ebfa ||
				pc==0x107728 || pc==0x107bc4 || pc==0x108118 || pc==0x10814c || pc==0x17c084)
			{
				*trace << "midi-enter pc=0x" << std::hex << pc << " from=0x" << lastTracePc << " d0=" << cpu.getDReg(0)
					<< " d1=" << cpu.getDReg(1) << " d2=" << cpu.getDReg(2)
					<< " d3=" << cpu.getDReg(3) << " d4=" << cpu.getDReg(4) << " d5=" << cpu.getDReg(5) << " d6=" << cpu.getDReg(6)
					<< " a0=" << cpu.getAReg(0) << " a1=" << cpu.getAReg(1)
					<< " a2=" << cpu.getAReg(2) << " a7=" << cpu.getAReg(7) << '\n';
			}
		}
		uint32_t call(uint32_t address, std::initializer_list<uint16_t> arguments, uint64_t budget, bool inspection=false)
		{
            if(!inspection) runDsp();
			const auto state=*cpu.getCpuState();
            struct Restore
            {
                Microcontroller& cpu; mc68k::CpuState state;
                ~Restore() {*cpu.getCpuState()=state;cpu.inspection=false;cpu.refreshInterruptLevel();}
            } restore{cpu,state};
            cpu.inspection=inspection;
			const uint32_t sp=cpu.getAReg(7)-4-2*static_cast<uint32_t>(arguments.size());
			cpu.write16(sp,0); cpu.write16(sp+2,0x800);
			uint32_t pos=sp+4;
			for(auto value:arguments) {cpu.write16(pos,value);pos+=2;}
			cpu.getCpuState()->dar[15]=sp;
			cpu.setPC(address);
            if(inspection) m68k_set_reg(cpu.getCpuState(),M68K_REG_SR,m68k_get_reg(cpu.getCpuState(),M68K_REG_SR)|0x700);
			if(trace) *trace << "call " << std::hex << address << '\n';
			for(uint64_t i=0;i<budget;++i)
			{
                if(inspection) m68k_execute(cpu.getCpuState(),1); else step();
				if(cpu.getPC()==0x800)
				{
					const auto result=cpu.getDReg(0);
					if(trace) *trace << "return " << std::hex << address << " = " << result << '\n';
					return result;
				}
				if(cpu.getPC()==0x102ed8) throw std::runtime_error("Firmware call entered exception handler");
			}
			std::ostringstream error;
			error<<"Firmware call budget exhausted at 0x"<<std::hex<<address
				<<" CPU=0x"<<cpu.getPC()<<" DSP=0x"<<dsp.getPC().var
				<<" D1=0x"<<cpu.getDReg(1)<<" SP=0x"<<cpu.getAReg(7)<<" stack=";
			for(unsigned i=0;i<32;i+=4) error<<' '<<ram32(cpu.getAReg(7)+i);
			throw std::runtime_error(error.str());
		}
		void transfer()
		{
			auto& hi=peripherals.getHI08();
			if(cpu.host.canReceiveData()&&hi.hasTX()) cpu.host.writeRx(hi.readTX());
		}
		// Setup only: application entry at 0x100abe still runs 0x104af0,
		// which recompiles and clears notes. MIDI is safe at the main loop.
		void finishStartup(uint64_t budget)
		{
			for(uint64_t i=0;i<budget;++i)
			{
				if(cpu.getPC()==0x100b38)
				{
					return;
				}
				step();
			}
			throw std::runtime_error("Application startup budget exhausted");
		}
        void runDsp()
        {
            if(!uploaded || dspCredit<=0) return;
            peripherals.getHI08().setHostFlags((cpu.host.icr()>>3)&1,(cpu.host.icr()>>4)&1);
            executeDspCredit();
            transfer();
        }
        // DSP-owned during an audio grant. MCU state is never accessed here
        // when the experimental worker is enabled (diagnostics are rejected).
        void executeDspCredit()
        {
            const auto begin=dsp.getCycles();
            while(dspCredit>0)
            {
                if(dsp.getPC().toWord()>=memory.sizeP()) throw std::runtime_error("DSP PC outside program memory");
                if(trace && dsp.getPC().var<dspBlockEntries.size()) ++dspBlockEntries[dsp.getPC().var];
                const auto before=dsp.getCycles();
                const bool native=burstFrames && capturing && !trace && !diagnostics && !coefficientTrace;
                const auto dspPc=dsp.getPC().var;
                const auto coefficient=coefficientTrace?memory.get(dsp56k::MemArea_X,0x96):0;
                if(native) dsp.getJit().getTrampoline().execUntilCycle(&dsp,before+uint64_t(std::ceil(dspCredit)));
                else dsp.exec();
                if(coefficientTrace && coefficient!=memory.get(dsp56k::MemArea_X,0x96))
                    {*coefficientTrace<<std::dec<<dsp.getCycles()<<','<<frames/2<<','<<std::hex<<dspPc<<','<<cpu.getPC()<<','<<coefficient<<','<<memory.get(dsp56k::MemArea_X,0x96);for(unsigned i=0;i<48;i+=4) *coefficientTrace<<','<<ram32(cpu.getAReg(7)+i);*coefficientTrace<<'\n';}
                // The core suppresses peripheral polling during long interrupts.
                // The Nord renders its entire sample graph in IRQD: DMA must
                // still clear the next mix buffer before that graph writes it.
                // Deadline-gated polling keeps peripherals running independently
                // of interrupt acceptance, without a per-sample host DSP path.
                if(diagnostics)
                {
                    const auto target=*peripherals.getTargetClockPtr();
                    const auto now=dsp.getInstructionCounter();
                    if(now>target) diagnostic.maxPeripheralBoundaryOverrun=std::max(diagnostic.maxPeripheralBoundaryOverrun,now-target);
                    diagnostic.maxBlockCycles=std::max(diagnostic.maxBlockCycles,dsp.getCycles()-before);
                    diagnostic.maxControlBacklog=std::max(diagnostic.maxControlBacklog,memory.get(dsp56k::MemArea_X,1));
                    diagnostic.maxQueuedAudio=std::max(diagnostic.maxQueuedAudio,uint32_t(audioWrite-audioRead));
                    recordDmaBoundary(dspPc,1);
                    dsp.execPeriph<dsp56k::Peripherals56303,dsp56k::PeripheralsNop>();
                    recordDmaBoundary(dspPc,2);
                }
                else dsp.execPeriph<dsp56k::Peripherals56303,dsp56k::PeripheralsNop>();
                dspCredit-=static_cast<double>(std::max<uint64_t>(1,dsp.getCycles()-before));
            }
            if(captureOverflow) throw std::runtime_error("Audio capture ring overflow");
            ++burstCount;const auto elapsed=dsp.getCycles()-begin;
            burstCycles+=elapsed;burstMaxCycles=std::max(burstMaxCycles,elapsed);
            if(capture) {++capture->bursts;capture->burstCycles+=elapsed;capture->maxBurstCycles=std::max(capture->maxBurstCycles,elapsed);}
        }
        void serviceAudioHostIrq(uint8_t vector)
        {
            // The HDI barrier has returned exclusive DSP ownership. Nord Lead
            // also waits for host-command completion: leaving the DSP parked
            // here can fill its external IRQ queue during a graph upload.
            peripherals.getHI08().setHostFlags((cpu.host.icr()>>3)&1,(cpu.host.icr()>>4)&1);
            dsp.processExternalInterrupts();
            audioHostDone=false;
            dsp.injectExternalInterrupt(vector);
            dsp.injectExternalInterrupt(audioHostFence);
            const auto begin=dsp.getCycles();
            while(!audioHostDone)
            {
                if(dsp.getCycles()-begin>=864*64)
                    throw std::runtime_error("Audio-driven host interrupt did not complete");
                dsp.getJit().getTrampoline().execUntilCycle(&dsp,dsp.getCycles()+64);
                dsp.execPeriph<dsp56k::Peripherals56303,dsp56k::PeripheralsNop>();
            }
            dspCredit-=double(dsp.getCycles()-begin);
            transfer();
            // Only HC service can add DSP audio while the MCU consumes its slice.
            // End that slice at this ownership boundary, not with ring polling
            // after every MCU instruction. The original allowance still accounts
            // for unfinished MCU work at the end of audioBatch.
            if(audioWrite-audioRead>=audio.size()/2) {
                audioCpuAllowance=0;
                if(diagnostics) ++diagnostic.backpressureYields;
            }
            if(captureOverflow) throw std::runtime_error("Audio capture ring overflow during host interrupt");
        }
        void waitAudioDsp()
        {
            if(!audioPending) return;
            // Clear even when wait rethrows: wait has already returned ownership.
            audioPending=false;
            audioWorker->wait();
            transfer();
        }
        void audioBatch(uint64_t& used,uint64_t budget)
        {
            // First settle any MCU-led control work from outside renderInto.
            runDsp();
            const auto cycles=uint64_t(audioQuantum)*864;
            const double ratio=double(std::max<uint64_t>(12288000,peripherals.getEssiClock().getSpeedInHz()))/cpu.getSim().getSystemClockHz();
            const auto cpuBegin=cpu.getCycles();
            // Preserve full audio batches in steady state. Only host-induced
            // debt of at least a batch needs an MCU-only catch-up slice; folding
            // every fractional overshoot into the DSP grant fragments playback.
            const bool catchUp=dspCredit<=-double(cycles);
            const double cpuAllowance=(catchUp?double(cycles):double(cycles)-dspCredit)/ratio;
            peripherals.getHI08().setHostFlags((cpu.host.icr()>>3)&1,(cpu.host.icr()>>4)&1);
            dspCredit=catchUp?dspCredit+double(cycles):double(cycles);
            audioCpuAllowance=cpuAllowance;
            audioEpoch=true;
            try
            {
                if(audioWorker) {audioPending=true;audioWorker->start();}
                else executeDspCredit(); // Production: retain exclusive ownership on this worker.
                // Like Nord Lead, MCU instructions consume an audio-time cycle
                // allowance. HDI accesses rendezvous with this DSP grant first.
                while(double(cpu.getCycles()-cpuBegin)<audioCpuAllowance)
                {
                    if(used++==budget) throw std::runtime_error("Audio frame budget exhausted");
                    step();
                }
                waitAudioDsp();
            }
            catch(...)
            {
                const auto error=std::current_exception();
                try {waitAudioDsp();} catch(...) {}
                audioEpoch=false;std::rethrow_exception(error);
            }
            // Carry fractional MCU cycles and JIT block overshoot into the next
            // grant, so neither processor's clock drifts over long playback.
            dspCredit+=(double(cpu.getCycles()-cpuBegin)-cpuAllowance)*ratio;
            audioEpoch=false;
        }
		void step()
		{
            if(cancelGeneration && !(steps&2047) && (cancelGeneration->load(std::memory_order_relaxed)!=expectedGeneration || cancelStop->load(std::memory_order_relaxed))) throw Hardware::Cancelled{};
			++steps;
			const auto pc=cpu.getPC();
			if(patchLoaded && pc==0x100b38) lastMainLoop=cpu.getCycles();
			if(pc!=lastTracePc) traceMidiEntry(pc);
			lastTracePc=pc;
			if(trace && pc==0x101abc) *trace << "midi-rx " << std::hex << cpu.getDReg(0) << "\n";
			if(trace && pc>=0x102dce && pc<=0x102e70) *trace << "startup " << std::hex << pc << '\n';
			const auto cpuCycles=cpu.exec();
			cpu.pcPort.advance(cpuCycles,cpu.getSim().getSystemClockHz());
			if(cpu.pcPort.interrupt() && (cpu.read16(0xfff920)&0x20) && cpu.pcPort.takeInterrupt()) cpu.getGPT().injectInterrupt(0xa);
			traceMidiQueues();
			if(uploaded && !audioEpoch)
			{
				dspCredit+=static_cast<double>(cpuCycles)*std::max<uint64_t>(12288000,peripherals.getEssiClock().getSpeedInHz())/cpu.getSim().getSystemClockHz();
                if(!burstFrames || !capturing || !patchLoaded || dspCredit>=double(burstFrames)*864) runDsp();
			}
			if(!audioEpoch && cpu.getPC()==0x102dec)
			{
				struct Block {uint32_t offset,count,address; dsp56k::EMemArea area;};
				for(auto b:{Block{0x4aeb2,0x80,0x640,dsp56k::MemArea_X},Block{0x4b0b2,0x80,0x640,dsp56k::MemArea_Y},Block{0x4b5b2,0x100,0x700,dsp56k::MemArea_X},Block{0x4b9b2,0x80,0x700,dsp56k::MemArea_Y},Block{0x4b3b2,0x80,0x780,dsp56k::MemArea_Y}})
					for(unsigned i=0;i<b.count;++i)
						if(memory.get(b.area,b.address+i)!=rom.word(b.offset+4*i)) throw std::runtime_error("DSP initialization table mismatch");
				tables=true; milestone="DSP initialization tables verified";
			}
		}
	};
	Hardware::Hardware(const std::string& firmware,std::ostream* trace):Hardware(std::make_shared<const Rom>(firmware),trace){}
    Hardware::Hardware(std::shared_ptr<const Rom> firmware,std::ostream* trace):m_impl(std::make_unique<Impl>(std::move(firmware),trace)){}
    void Hardware::setCancellation(const std::atomic<unsigned>* generation,unsigned expected,const std::atomic<bool>* stop)
    {m_impl->cancelGeneration=generation;m_impl->expectedGeneration=expected;m_impl->cancelStop=stop;}
    void Hardware::clearCancellation() {m_impl->cancelGeneration=nullptr;m_impl->cancelStop=nullptr;}
	Hardware::~Hardware()=default;
    void Hardware::enableCompilationDiagnostics()
    {
        auto& h=*m_impl;if(h.compilationDiagnostics) return;
        h.compilationDiagnostics=true;
        auto config=h.dsp.getJit().getConfig();auto previous=config.getBlockConfig;
        config.getBlockConfig=[&h,previous](dsp56k::TWord pc)->std::optional<dsp56k::JitConfig> {
            ++h.compilations;return previous?previous(pc):std::optional<dsp56k::JitConfig>{};
        };
        h.dsp.getJit().setConfig(config);
    }
    uint64_t Hardware::compilationCount() const {return m_impl->compilations;}
    void Hardware::enableDmaBoundaryTrace(size_t capacity)
    {
        if(!capacity || capacity>1048576) throw std::runtime_error("DMA trace capacity must be 1..1048576");
        enableRuntimeDiagnostics();
        auto& h=*m_impl;h.dmaBoundaries.resize(capacity);h.dmaBoundaryCount=0;h.dmaBoundaryFull=false;
    }
    void Hardware::writeDmaBoundaryTrace(std::ostream& out) const
    {
        const auto& h=*m_impl;
        out<<"# boundary observations, not instruction-exact bus events; truncated="<<h.dmaBoundaryFull<<"\n";
        out<<"cycles,frame,last_dma_clock,pc,next_pc,phase,dcr,ddr,pending_clocks,mix_selector,y6c0,y6c1,y6e0,y6e1,dcr4,dcr5\n";
        for(size_t i=0;i<h.dmaBoundaryCount;++i) {
            const auto& e=h.dmaBoundaries[i];
            out<<std::dec<<e.cycles<<','<<e.frame<<','<<e.clock<<','<<e.pc<<','<<e.next<<','<<e.phase<<','<<e.dcr<<','<<e.ddr<<','<<e.pending<<','<<e.mixSelector<<','<<e.left<<','<<e.right<<','<<e.otherLeft<<','<<e.otherRight<<','<<e.tx4<<','<<e.tx5<<'\n';
        }
    }
    void Hardware::setJitBlockLimit(unsigned instructions, bool deadlineGraphs)
    {
        auto& h=*m_impl;
        if(h.steps || (instructions!=16 && instructions!=32 && instructions!=64))
            throw std::runtime_error("Set JIT block limit to 16, 32 or 64 before boot");
        h.deadlineGraphs=deadlineGraphs;
        auto config=h.dsp.getJit().getConfig();
        // Longer native execution uses checked continuations between the existing
        // 16-instruction blocks. Their clock increments and DMA write timestamps
        // stay bounded; every edge checks peripheral and owner deadlines.
        // No graph-address heuristic or cached classification survives uploads.
        config.maxInstructionsPerBlock=deadlineGraphs?16:instructions;
        config.linkInstructionLimitBlocks=config.linkJitBlocks && config.linkedBlockDeadlineChecks && deadlineGraphs;
        h.dsp.getJit().setConfig(config);
    }
    void Hardware::setDeadlineLinkedJit(bool enabled, bool instructionContinuations)
    {
        auto& h=*m_impl;if(h.steps) throw std::runtime_error("Configure JIT linking before boot");
        auto config=h.dsp.getJit().getConfig();config.linkJitBlocks=enabled;config.linkedBlockDeadlineChecks=enabled;config.linkInstructionLimitBlocks=enabled && (instructionContinuations || h.deadlineGraphs);
        h.dsp.getJit().setConfig(config);
    }
    void Hardware::setAudioDrivenExecution(uint32_t frames,bool threaded)
    {
        auto& h=*m_impl;
        if(h.steps || frames>64) throw std::runtime_error("Configure audio-driven execution before boot, with 0..64 frames");
        if(frames && threaded && (h.trace || h.capture || h.diagnostics || h.coefficientTrace))
            throw std::runtime_error("Audio-driven experiment does not support tracing/capture");
        h.audioQuantum=frames;
        if(frames) {
            if(!h.audioHostFence) h.audioHostFence=h.dsp.registerInterruptFunc([&h]{h.audioHostDone=true;});
            if(threaded) h.audioWorker=std::make_unique<AudioWorker>([&h]{h.executeDspCredit();});
            else h.audioWorker.reset();
        }
        else h.audioWorker.reset();
    }
    void Hardware::setCapture(Capture* capture)
    {
        auto& h=*m_impl;
        if(capture && h.audioWorker) throw std::runtime_error("Capture requires single-worker execution");
        h.capture=capture;
        if(capture) {
            capture->startCycles=h.dsp.getCycles();capture->startFrames=h.frames/2;
            h.peripherals.getHI08().setReadRxCallback([&h]{if(h.capture) h.capture->push(h.dsp.getCycles(),h.frames/2,Capture::DspReceive,{uint32_t(h.dsp.getPC().var),uint32_t(h.dsp.regs().r[0].var),uint32_t(h.dsp.regs().r[1].var)},h.cpu.getCycles());});
        } else h.peripherals.getHI08().setReadRxCallback({});
    }
    void Hardware::setDspExecutionWindow(uint32_t frameCount)
    {
        if(frameCount>32) throw std::runtime_error("DSP window must be 0..32 frames");
        m_impl->runDsp();m_impl->burstFrames=frameCount;
    }
	void Hardware::boot(uint64_t budget)
	{
		if(m_impl->booted) throw std::runtime_error("Hardware is already booted");
		for(uint64_t i=0;i<budget;++i)
		{
			m_impl->step();
			if(m_impl->cpu.getPC()==0x100abe) {if(!m_impl->uploaded || !m_impl->tables) throw std::runtime_error("Incomplete DSP initialization");m_impl->booted=true;m_impl->milestone="OS initialized";return;}
			if(m_impl->cpu.getPC()==0x102ed8) throw std::runtime_error("CPU entered default exception handler");
		}
		throw std::runtime_error("Boot instruction budget exhausted");
	}
	void Hardware::initializePatch(uint64_t budget)
	{
		auto& h=*m_impl;
		if(!h.booted || h.patchLoaded) throw std::runtime_error("Patch loading requires a freshly booted machine");
		h.call(0x109b44,{0,0,1,7},budget);
		h.call(0x109b44,{0,0,2,5},budget);
		h.milestone="minimal modules allocated";
		// Firmware cable record: input module/port, output module/(port | 0x40), color, reserved.
		const uint8_t cable[]{2,0,1,64,0,0};
		for(unsigned i=0;i<6;++i) h.cpu.write8(0x1f0000+i,cable[i]);
		h.call(0x1119a4,{0,0,0x1f,0},budget);
		// Set oscillator pitch and output level through the firmware parameter setter.
		h.call(0x109c4c,{1,0,0,1,0,64},budget);
		h.call(0x109c4c,{1,0,0,1,1,64},budget);
		h.call(0x109c4c,{1,0,0,2,0,127},budget);
		// Compile through the complete firmware orchestration path.
		h.cpu.write8(0x17c05f,1);
		h.call(0x109706,{100,1},budget);
		const auto result=h.call(0x1049c0,{0,0},budget);
		if(result!=1) throw std::runtime_error("Firmware rejected minimal patch");
		h.patchLoaded=true;
		h.finishStartup(budget);
		h.milestone="minimal patch compiled";
		if(h.trace) {report(*h.trace); for(uint32_t a=0x175;a<0x400;++a) *h.trace << "program " << std::hex << a << " " << h.memory.get(dsp56k::MemArea_P,a) << "\n";}
	}
	void Hardware::loadPatch(const std::string& filename, uint64_t budget)
	{
		loadPatch(Patch::load(filename),budget);
	}
	void Hardware::loadPatch(const Patch& patch, uint64_t budget)
	{
		auto& h=*m_impl;
		if(!h.booted || h.patchLoaded) throw std::runtime_error("Patch loading requires a freshly booted machine");
		for(auto m:patch.modules)
		{
			h.call(0x109b44,{0,m.area,m.index,m.type},budget);
			const auto record=h.ram32(0x175f40+(m.area?0x4c66:0x467a)+4*m.index);
			h.cpu.write8(record+15,m.x);h.cpu.write8(record+16,m.y);
			for(unsigned i=0;i<16;++i) h.cpu.write8(record+19+i,i<m.name.size()?uint8_t(m.name[i]):0);
		}
        for(const auto& custom:patch.custom)
        {
            const auto size=h.call(0x109e72,{0,custom.area,custom.module},budget);
            if(custom.data.size()!=size) throw std::runtime_error("Custom module data size differs from firmware schema");
            const auto address=h.call(0x109df8,{0,custom.area,custom.module},budget);
            for(unsigned i=0;i<size;++i) h.cpu.write8(address+i,custom.data[i]);
        }
        for(const auto& c:patch.controllers) h.call(0x10a060,{c.index,0,c.area,c.module,c.parameter},budget);
		for(unsigned i=0;i<16;++i) h.cpu.write8(0x17b962+i,i<patch.name.size()?uint8_t(patch.name[i]):0);
		for(auto c:patch.cables)
		{
			for(unsigned i=0;i<c.record.size();++i) h.cpu.write8(0x1f0000+i,c.record[i]);
			h.call(0x1119a4,{0,c.area,0x1f,0},budget);
		}
		for(auto p:patch.parameters) h.call(0x109c4c,{1,0,p.area,p.module,p.index,p.value},budget);
		for(const auto& k:patch.knobs) if(k.index<23) h.call(0x10f2c6,{k.index,0,k.area,k.module,k.parameter},budget);
		// Keep ranges and interpolation in the MCU/DSP compiler; do not bake a
		// host-side offset into the base parameters.
		for(auto m:patch.morphs)
			if(!h.call(0x10b98a,{0,m.area,m.module,m.parameter,uint16_t(m.range),m.group},budget))
				throw std::runtime_error("Firmware rejected morph mapping");
		for(uint16_t group=0;group<4;++group)
		{
			h.call(0x109c4c,{1,0,2,1,group,patch.morphValues[group]},budget);
			h.call(0x10bab6,{0,group,patch.morphKeyboard[group]},budget);
		}
		// The event dispatcher filters both note and velocity against the active
		// voice's header ranges at offsets 0x5a1b..0x5a1e. These setters are the
		// firmware's public helpers for those fields; omitting them leaves the
		// velocity upper bound at zero and silently rejects note-on events.
		h.call(0x10c308,{0,patch.keyRangeMin,patch.keyRangeMax},budget);
		h.call(0x10c35c,{0,patch.velocityRangeMin,patch.velocityRangeMax},budget);
		h.call(0x10a278,{0,patch.requestedVoices},budget);
		h.call(0x10c2be,{0,patch.bendRange},budget);
        h.call(0x10c1ee,{0,patch.portamento},budget);
        h.call(0x10c236,{0,patch.portamentoTime},budget);
        h.call(0x1011ae,{0,patch.octaveShift},budget);
        h.call(0x10c384,{0,patch.areaSeparator},budget);
		h.call(0x10c1c8,{0,1,patch.voiceRetrigger},budget);
		h.call(0x10c1c8,{0,0,patch.commonRetrigger},budget);
		h.cpu.write8(0x17c05f,1);
		h.call(0x109706,{100,1},budget);
		if(h.call(0x1049c0,{0,0},budget)!=1) throw std::runtime_error("Firmware rejected patch");
		h.patchLoaded=true;
		h.finishStartup(budget);
		h.milestone="patch compiled";
		if(h.trace) {report(*h.trace); for(uint32_t a=0x175;a<0x400;++a) *h.trace << "program " << std::hex << a << " " << h.memory.get(dsp56k::MemArea_P,a) << "\n";}
	}
	void Hardware::setPatchParameter(uint16_t area,uint16_t module,uint16_t parameter,uint8_t value)
	{
		auto& h=*m_impl;
		if(!h.patchLoaded || area>2 || module<1 || module>127 || parameter>127 || value>127 || (area==2 && (module!=1 || parameter>3)))
			throw std::runtime_error("Invalid live patch parameter");
		// Preserve ESSI frames produced while the firmware services the setter.
		h.capturing=h.captureControlAudio;
		// Match the editor parameter path: store the value, then run the OS
		// conversion/broadcast helper for every allocated voice. The storage
		// setter alone never applies coefficients to the DSP.
		try
		{
			h.call(0x109c4c,{1,0,area,module,parameter,value},1000000);
			h.call(0x106d40,{0,area,module,parameter,value},1000000);
		}
		catch(...) {h.capturing=false;throw;}
		h.capturing=false;
	}
    void Hardware::setControlAudioCapture(bool enabled) {m_impl->captureControlAudio=enabled;}
	void Hardware::setMasterVolume(uint8_t value)
	{
		auto& h=*m_impl;
		if(!h.patchLoaded || value>127) throw std::runtime_error("Invalid master volume");
		h.capturing=h.captureControlAudio;
		try {h.call(0x109706,{value,1},1000000);}
		catch(...) {h.capturing=false;throw;}
		h.capturing=false;
	}
	uint32_t Hardware::readMemory(char space,uint32_t address) const
	{
		const auto& h=*m_impl;
		if(space=='C' && address<h.cpu.ram.size()) return h.ram8(address);
		if(address<0x20000)
		{
			if(space=='P') return h.memory.get(dsp56k::MemArea_P,address);
			if(space=='X') return h.memory.get(dsp56k::MemArea_X,address);
			if(space=='Y') return h.memory.get(dsp56k::MemArea_Y,address);
		}
		throw std::runtime_error("Invalid diagnostic memory address");
	}
	bool Hardware::sendEditorMidi(const uint8_t* data,size_t size) {return m_impl->cpu.pcPort.receive(data,size);}
	std::vector<uint8_t> Hardware::receiveEditorMidi() {return m_impl->cpu.pcPort.takeOutput();}
	bool Hardware::editorIdle() const
	{
		auto& h=*m_impl;const auto clock=h.cpu.getSim().getSystemClockHz();
		return h.cpu.pcPort.idle(clock) && h.cpu.getCycles()-h.lastMainLoop<clock/100;
	}
    bool Hardware::editorSnapshotReady() const
    {
        auto& h=*m_impl;const auto clock=h.cpu.getSim().getSystemClockHz();
        return h.cpu.pcPort.inputIdle(clock) && h.cpu.getCycles()-h.lastMainLoop<clock/100;
    }
	std::string Hardware::editorPatchName() const
	{
		std::string name;for(unsigned i=0;i<16;++i) {auto b=m_impl->ram8(0x17b962+i);if(!b) break;name.push_back(char(b));}return name;
	}
	void Hardware::notifyEditorPatchChanged()
	{
		auto& h=*m_impl;h.capturing=h.captureControlAudio;
        try
        {
            // 103B46 waits on the software TX length at 172FFC. The main
            // loop drains that buffer; injecting the helper while it is busy
            // prevents the suspended loop from making progress. Finish the
            // native transaction at a safe main-loop boundary first.
            uint64_t budget=1000000;
            while(h.cpu.getPC()!=0x100b38 || h.ram16(0x172ffc))
            {
                if(!budget--) throw std::runtime_error("Editor notification transmit budget exhausted");
                h.step();
            }
            h.call(0x103b46,{0},1000000);
        }
        catch(...) {h.capturing=false;throw;}
		h.capturing=false;
	}
	std::array<Hardware::EditorKnob,3> Hardware::editorKnobs() const
	{
		const auto& h=*m_impl;std::array<EditorKnob,3> result{};
		for(unsigned i=0;i<3;++i)
		{
			auto& k=result[i];const auto address=0x17bb76+4*i;
			k.area=h.ram8(address);k.module=h.ram8(address+1);k.parameter=h.ram8(address+2);
			if(k.area>2 || !k.module || k.module>127 || (k.area==2 && (k.module!=1 || k.parameter>3))) {k={};continue;}
			if(k.area==2) k.value=h.ram8(0x175f40+0x5d64+k.parameter);
			else
			{
				const auto record=h.ram32(0x175f40+(k.area?0x4c66:0x467a)+4*k.module);
				if(record<0x165c30 || record>=0x169c30 || !h.ram8(record+14)) {k={};continue;}
				k.value=h.ram8(record+0x29+8*k.parameter);
			}
		}
		return result;
	}
	Hardware::EditorKnob Hardware::editorButton() const
	{
		const auto& h=*m_impl;EditorKnob result{};
		const auto address=0x17bb76+4*3;
		result.area=h.ram8(address);result.module=h.ram8(address+1);result.parameter=h.ram8(address+2);
		if(result.area>2 || !result.module || result.module>127 || (result.area==2 && (result.module!=1 || result.parameter>3))) return {};
		if(result.area==2) result.value=h.ram8(0x175f40+0x5d64+result.parameter);
		else
		{
			const auto record=h.ram32(0x175f40+(result.area?0x4c66:0x467a)+4*result.module);
			if(record<0x165c30 || record>=0x169c30 || !h.ram8(record+14)) return {};
			result.value=h.ram8(record+0x29+8*result.parameter);
		}
		return result;
	}
	void Hardware::restoreEditorPatch(const std::vector<uint8_t>& messages)
	{
		if(!validEditorPatch(messages)) throw std::runtime_error("Invalid saved editor patch packets");
        std::array<std::array<float,2>,256> discard{};
		size_t begin=0;
		while(begin<messages.size())
		{
			const auto it=std::find(messages.begin()+begin,messages.end(),0xf7);
			if(it==messages.end()) throw std::runtime_error("Truncated saved editor patch");
			const auto end=size_t(it-messages.begin())+1;
			if(end-begin<7 || end-begin>256 || messages[begin]!=0xf0 || messages[begin+1]!=0x33 || messages[begin+3]!=6)
				throw std::runtime_error("Invalid saved editor patch");
			receiveEditorMidi();
			if(!sendEditorMidi(messages.data()+begin,end-begin)) throw std::runtime_error("Editor restore queue full");
			bool acknowledged=false;std::vector<uint8_t> reply;
			for(unsigned i=0;i<750 && !acknowledged;++i)
			{
				renderInto(discard.data(),256,1000000);auto bytes=receiveEditorMidi();
				for(auto b:bytes)
				{
					if(b==0xf0) reply.clear();reply.push_back(b);
					if(b==0xf7 && reply.size()>=8 && reply[0]==0xf0 && reply[2]==0x58 && (reply[5]==0x36 || reply[5]==0x7f)) acknowledged=true;
				}
			}
			if(!acknowledged)
			{
				std::ostringstream error;error << "Firmware did not acknowledge saved editor patch at " << begin << ":";
				for(auto b:reply) error << ' ' << std::hex << unsigned(b);
				throw std::runtime_error(error.str());
			}
			begin=end;
		}
	}
	std::vector<std::vector<uint8_t>> Hardware::exportEditorPatch(uint32_t chunkBytes)
	{
		auto& h=*m_impl;
		if(!chunkBytes || chunkBytes>166) throw std::runtime_error("Invalid editor packet size");
		std::vector<std::vector<uint8_t>> sections;
		std::vector<uint8_t> name{55,0,0,0};
		for(unsigned i=0;i<16;++i) {const auto b=h.ram8(0x17b962+i);name.push_back(b);if(!b) break;}
		sections.push_back(std::move(name));
		// Read-only serializers run as an inspection: no extra device time,
        // UART bytes or audio frames are generated while taking a snapshot.
		{
			// The same routines used by the firmware's flash patch writer (1212b0).
			for(const auto entry:std::initializer_list<std::pair<uint32_t,int>>{
				{0x11840a,-1},{0x11617a,1},{0x11617a,0},{0x118bec,-1},
				{0x116ba6,1},{0x116ba6,0},{0x1176a0,1},{0x1176a0,0},
				{0x117b1c,-1},{0x116576,-1},{0x116836,-1},
				{0x11802e,1},{0x11802e,0},{0x117348,1},{0x117348,0}})
			{
				std::fill_n(h.cpu.ram.begin()+0x1f0000,0x8000,0);
				const auto size=uint16_t(entry.second<0?h.call(entry.first,{0x1f,0,0},1000000,true):h.call(entry.first,{0x1f,0,0,uint16_t(entry.second)},1000000,true));
				if(!size || size>0x8000) throw std::runtime_error("Invalid native patch section size at "+std::to_string(entry.first)+": "+std::to_string(size));
				sections.emplace_back(h.cpu.ram.begin()+0x1f0000,h.cpu.ram.begin()+0x1f0000+size);
			}
		}
		std::vector<uint8_t> bytes;
		std::vector<size_t> ends;
		for(const auto& section:sections) {bytes.insert(bytes.end(),section.begin(),section.end());ends.push_back(bytes.size());}
		std::vector<std::vector<uint8_t>> messages;
		for(size_t begin=0;begin<bytes.size();begin+=chunkBytes)
		{
			const auto end=std::min(begin+chunkBytes,bytes.size());
			const uint8_t command=bytes.size()<=chunkBytes?0x7c:begin==0?0x74:end==bytes.size()?0x78:0x70;
			unsigned count=0;for(auto e:ends) if(e>begin && e<=end) ++count;
			std::vector<uint8_t> message{0xf0,0x33,command,6,uint8_t(0x40|count)};
			for(size_t bit=0;bit<(end-begin)*8;bit+=7)
			{
				unsigned value=0;
				for(unsigned i=0;i<7;++i) {value<<=1;const auto pos=bit+i;if(pos<(end-begin)*8) value|=(bytes[begin+pos/8]>>(7-pos%8))&1;}
				message.push_back(value);
			}
			unsigned checksum=0;for(auto b:message) checksum+=b;
			message.push_back(checksum&127);message.push_back(0xf7);messages.push_back(std::move(message));
		}
		return messages;
	}
	void Hardware::sendMidi(uint8_t status,uint8_t data1,uint8_t data2)
	{
		if(status<0x80 || status>=0xf0 || data1>127 || data2>127) throw std::runtime_error("Invalid MIDI channel message");
		if(m_impl->trace) *m_impl->trace << "midi-send " << std::hex << unsigned(status) << ' ' << unsigned(data1) << ' ' << unsigned(data2) << '\n';
        if(m_impl->capture) m_impl->capture->push(m_impl->dsp.getCycles(),m_impl->frames/2,Capture::Midi,{status,data1,data2},m_impl->cpu.getCycles());
		auto& qsm=m_impl->cpu.getQSM();
		qsm.writeSciRX(status);qsm.writeSciRX(data1);
		if((status&0xf0)!=0xc0 && (status&0xf0)!=0xd0) qsm.writeSciRX(data2);
	}
	std::vector<std::array<float,2>> Hardware::render(uint32_t count,uint64_t budget)
	{
		std::vector<std::array<float,2>> result(count);
        renderInto(result.data(),count,budget);
        return result;
    }
    void Hardware::renderInto(std::array<float,2>* output,uint32_t count,uint64_t budget,const std::array<float,2>* input)
    {
        auto& h=*m_impl;
        if(!h.patchLoaded) throw std::runtime_error("Load a patch before rendering");
        h.capturing=true;
        try
        {
            uint64_t steps=0;
            for(uint32_t begin=0;begin<count;)
            {
                const auto size=std::min<uint32_t>(256,count-begin);
                if(h.inputWrite-h.inputRead+size>h.inputQueue.size()) throw std::runtime_error("Audio input ring overflow");
                for(unsigned i=0;i<size;++i) h.inputQueue[h.inputWrite++%h.inputQueue.size()]=input?input[begin+i]:std::array<float,2>{};
                uint32_t written=0;
                while(written<size)
                {
                    while(h.audioRead<h.audioWrite && written<size)
                        output[begin+written++]=h.audio[h.audioRead++%h.audio.size()];
                    if(written==size) break;
                    if(h.audioQuantum) h.audioBatch(steps,budget);
                    else {
                        if(steps++==budget) throw std::runtime_error("Audio frame budget exhausted");
                        h.step();
                    }
                }
                begin+=size;
            }
        }
        catch(...) {h.capturing=false;throw;}
        h.capturing=false;
        if(h.capture)
        {
            constexpr uint32_t poly=0x175f40+0x4c66;
            for(unsigned i=0;i<4;++i) {
                const auto a=poly+0x3e8+14*i;
                h.capture->push(h.dsp.getCycles(),h.frames/2,Capture::Voice,{i,h.ram8(a+9),h.ram8(a+12),h.ram16(poly+0x228+14*i+6)});
            }
            // Preserve raw float bits. Analysis/formatting is deferred to the reader.
            uint32_t l=0,r=0;if(count) {std::memcpy(&l,&output[count-1][0],4);std::memcpy(&r,&output[count-1][1],4);}
            h.capture->push(h.dsp.getCycles(),h.frames/2,Capture::Audio,{count,l,r,h.memory.get(dsp56k::MemArea_X,1)});
            if(h.capture->full) h.capture=nullptr;
        }
	}
	void Hardware::report(std::ostream& out) const
	{
		auto& h=*m_impl;
		double bestSerialAc=0;
		for(unsigned channel=0;channel<h.serialSum.size();++channel) if(h.serialCounts[channel])
			{
				const auto mean=h.serialSum[channel]/h.serialCounts[channel];
				bestSerialAc=std::max(bestSerialAc,std::sqrt(std::max(0.0,h.serialSquares[channel]/h.serialCounts[channel]-mean*mean)));
			}
		out << "milestone=" << h.milestone << "\nserial_diagnostics=" << (h.trace?"enabled":"disabled")
			<< "\nserial_ac_rms=" << std::setprecision(10) << bestSerialAc;
		for(unsigned channel=0;channel<h.serialSum.size();++channel) if(h.serialCounts[channel]) out << " serial" << channel << "_range=" << h.serialMin[channel] << ':' << h.serialMax[channel];
		out << "\ncpu_pc=0x" << std::hex << h.cpu.getPC() << " dsp_pc=0x" << h.dsp.getPC().var
			<< " sample_entry=0x" << h.memory.get(dsp56k::MemArea_P,0x17) << " dor0=0x" << h.peripherals.getDMA().getDOR(0) << " x1=0x" << h.memory.get(dsp56k::MemArea_X,1) << " outX=0x" << h.memory.get(dsp56k::MemArea_X,0x6c0) << " outY=0x" << h.memory.get(dsp56k::MemArea_Y,0x6c0) << " cpu_sr=0x" << m68k_get_reg(h.cpu.getCpuState(),M68K_REG_SR) << " ticks=0x" << mc68k::memoryOps::readU32(h.cpu.ram,0x17dd70) << std::dec << "\naudio_quantum="<<h.audioQuantum<< "\ndsp_bursts="<<h.burstCount<<" burst_mean_cycles="<<(h.burstCount?double(h.burstCycles)/h.burstCount:0)<<" burst_max_cycles="<<h.burstMaxCycles<<"\ncpu_steps=" << h.steps << " boot_words=" << h.bootWords.size() << " host_commands=" << h.commands << " serial_frames=" << h.frames << '\n';
	}
	Hardware::Timing Hardware::timing() const
	{
		const auto& h=*m_impl;
		auto result=h.diagnostic;result.cycles=h.dsp.getCycles();result.frames=h.frames/2;result.controlSampleCounter=h.memory.get(dsp56k::MemArea_X,1);return result;
	}

    void Hardware::enableRuntimeDiagnostics(std::ostream* coefficientTrace)
    {
        auto& h=*m_impl;
        if(h.audioWorker) throw std::runtime_error("Runtime diagnostics require single-worker execution");
        h.diagnostics=true;h.diagnostic={};h.irqRead=h.irqWrite=0;h.coefficientTrace=coefficientTrace;
        h.dsp.setInterruptObserver([](void* context,dsp56k::TWord vector,uint64_t cycles)
        {
            auto& h=*static_cast<Impl*>(context);
            if(vector!=0x16 || h.irqRead==h.irqWrite) return;
            if(h.irqWrite-h.irqRead<=h.irqCycles.size()) h.diagnostic.maxIrqAcceptanceCycles=std::max(h.diagnostic.maxIrqAcceptanceCycles,cycles-h.irqCycles[h.irqRead%h.irqCycles.size()]);
            ++h.irqRead;++h.diagnostic.irqObserved;
        },&h);
        if(coefficientTrace) *coefficientTrace<<"cycles,frame,dsp_block_pc,mcu_pc,old_x96,new_x96,stack0,stack4,stack8,stack12,stack16,stack20,stack24,stack28,stack32,stack36,stack40,stack44\n";
    }
    std::array<Hardware::FlashPatch,99> Hardware::flashPatches() const
    {
        // OS directory: 170A68 base, 170A6E count, six-byte records
        // {object ID, fragment flags, 128-byte flash page}. See 12041A/11F78E.
        const auto& h=*m_impl;std::array<FlashPatch,99> result{};
        const auto base=h.ram32(0x170a68);const auto count=h.ram16(0x170a6e);
        const auto& bytes=h.cpu.flash.data();
        if(count>(bytes.size()-0x70000)/128 || base>=h.cpu.ram.size() || uint64_t(base)+6*count>h.cpu.ram.size()) return result;
        for(unsigned i=0;i<count;++i)
        {
            const auto record=base+6*i;const auto id=h.ram16(record);
            if(id<1 || id>99 || h.ram16(record+2)!=0x8000) continue;
            const auto page=h.ram16(record+4);const auto address=0x70000u+128u*page;
            if(address+128>bytes.size() || bytes[address+4+21]!=7) continue;
            const auto length=(uint32_t(bytes[address+26])<<24)|(uint32_t(bytes[address+27])<<16)|(uint32_t(bytes[address+28])<<8)|bytes[address+29];
            const unsigned pages=(26u+length+123u)/124u;
            if(!length || length>65536 || pages>count-i) continue;
            auto& entry=result[id-1];uint64_t hash=1469598103934665603ull;bool valid=true;
            for(unsigned j=0;j<pages;++j)
            {
                const auto r=record+6*j,a=0x70000u+128u*h.ram16(r+4);
                if(h.ram16(r)!=id || a+128>bytes.size()) {valid=false;break;}
                for(unsigned k=0;k<128;++k) {hash^=bytes[a+k];hash*=1099511628211ull;}
            }
            if(!valid) continue;
            for(unsigned k=0;k<16 && bytes[address+4+k];++k) entry.name+=char(bytes[address+4+k]&127);
            if(entry.name.empty()) entry.name="Untitled";
            entry.fingerprint=hash?hash:1;
        }
        return result;
    }
    void Hardware::loadFlashPatch(unsigned position,uint64_t budget)
    {
        auto& h=*m_impl;
        if(!h.booted || h.patchLoaded || position>=99 || !flashPatches()[position].fingerprint)
            throw std::runtime_error("Native flash patch is unavailable");
        h.cpu.write8(0x1f0000,0);h.cpu.write8(0x1f0001,uint8_t(position));
        h.cpu.write8(0x17c05f,1);
        // Same loader as editor command 41/0A: location pointer, slot A, compile.
        h.call(0x121104,{0x1f,0,0,1},budget);
        h.patchLoaded=true;h.finishStartup(budget);h.milestone="flash patch compiled";
    }
    const std::vector<uint8_t>& Hardware::flashImage() const {return m_impl->cpu.flash.data();}
    uint64_t Hardware::flashRevision() const {return m_impl->cpu.flash.revision();}
    void Hardware::restoreFlash(const std::vector<uint8_t>& image)
    {if(m_impl->booted) throw std::runtime_error("Restore flash before boot");m_impl->cpu.flash.restore(image);}
	void Hardware::dumpState(std::ostream& out) const
	{
		auto& h=*m_impl;
		out << std::hex;
		out << "reg cpu_pc " << h.cpu.getPC() << '\n';
		out << "reg pc " << h.dsp.getPC().var << "\nreg la " << h.dsp.regs().la.var
			<< "\nreg lc " << h.dsp.regs().lc.var << "\nreg sr " << h.dsp.regs().sr.var << '\n';
		for(uint32_t a=0x175f40;a<0x17bf40;++a) out << "cpu " << a << ' ' << unsigned(h.ram8(a)) << '\n';
		for(auto area:{dsp56k::MemArea_P,dsp56k::MemArea_X,dsp56k::MemArea_Y})
				for(uint32_t a=0;a<0x800;++a) out << (area==dsp56k::MemArea_P?'P':area==dsp56k::MemArea_X?'X':'Y') << ' ' << a << ' ' << h.memory.get(area,a) << '\n';
		for(const auto& loop:h.dsp.getJit().getLoops()) out << "loop " << loop.first << ' ' << loop.second << '\n';
		for(const auto end:h.dsp.getJit().getLoopEnds()) out << "loopend " << end << " 1\n";
		if(h.trace) for(unsigned i=0;i<h.dspBlockEntries.size();++i) if(h.dspBlockEntries[i]) out << "block " << i << ' ' << h.dspBlockEntries[i] << '\n';
		out << std::dec;
	}
}

namespace nmm
{
    void Hardware::dumpJitMap(std::ostream& output,std::ostream* nativeAssembly) const
    {
        struct Snapshot final : dsp56k::DebuggerInterface
        {
            std::ostream& out;
            std::ostream* native;
            unsigned index=0;
            Snapshot(dsp56k::DSP& dsp,std::ostream& stream,std::ostream* assembly):DebuggerInterface(dsp),out(stream),native(assembly) {}
            void onJitBlockCreated(const dsp56k::JitDspMode& mode,const dsp56k::JitBlockRuntimeData* block) override
            {
                out<<std::hex<<reinterpret_cast<uintptr_t>(block->getFunc())<<' '<<block->codeSize()<<' '
                   <<block->getPCFirst()<<' '<<block->getPMemSize()<<' '<<mode.get();
                for(unsigned pc=block->getPCFirst();pc<block->getPCNext();++pc) out<<' '<<dsp().memory().get(dsp56k::MemArea_P,pc);
                out<<'\n';
                if(native)
                {
                    *native<<"// termination="<<unsigned(block->getInfo().terminationReason)
                           <<" instructions="<<std::dec<<block->getInfo().instructionCount
                           <<" child="<<std::hex<<block->getChild()
                           <<" fallthrough="<<block->getNonBranchChild()<<'\n';
                    // Inspection only: assemble as a Mach-O object for llvm-objdump,
                    // never link or execute these copied, relocated bytes.
                    *native<<".text\n.p2align 2\n_nmm_jit_"<<std::hex<<block->getPCFirst()<<'_'<<mode.get()<<'_'<<index++<<":\n";
                    const auto* bytes=reinterpret_cast<const uint8_t*>(block->getFunc());
                    for(size_t i=0;i<block->codeSize();++i)
                    {
                        *native<<(i%16?",":".byte ")<<"0x"<<unsigned(bytes[i]);
                        if(i%16==15 || i+1==block->codeSize()) *native<<'\n';
                    }
                }
            }
        } snapshot(m_impl->dsp,output,nativeAssembly);
        // This invokes only the enumeration callback. setDebugger/onAttach are
        // deliberately not called, so execution and JIT emission stay unchanged.
        m_impl->dsp.getJit().onDebuggerAttached(snapshot);
    }
}
