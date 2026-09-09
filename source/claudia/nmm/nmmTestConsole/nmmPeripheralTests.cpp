#include "mc68k/mc68k.h"
#include "dsp56kEmu/dsp.h"
#include "nmmLib/nmmpcport.h"
#include "nmmLib/nmmflash.h"
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main()
{
	try
	{
        for(unsigned chunk:{1u,7u,127u,6720u})
        {
            struct Cpu:mc68k::Mc68k {uint16_t readImm16(uint32_t) override {return 0;}} cpu;auto& q=cpu.getQSM();q.setSciRxCycleTiming(true);
            q.write16(mc68k::PeriphAddress::SciControl0,21);
            q.write16(mc68k::PeriphAddress::SciControl1,4);
            require(q.sciFrameCycles()==6720,"SCI MIDI divider/frame length");
            q.writeSciRX(0x90);q.writeSciRX(60);
            auto advance=[&](unsigned total){while(total) {auto n=std::min(total,chunk);q.exec(n);total-=n;}};
            advance(6719);require(!(q.read16(mc68k::PeriphAddress::SciStatus)&0x40),"No early SCI byte");
            advance(1);require(q.read16(mc68k::PeriphAddress::SciStatus)&0x40,"SCI receive deadline");
            require(q.read16(mc68k::PeriphAddress::SciData)==0x90,"SCI first byte");
            advance(6720);require(q.read16(mc68k::PeriphAddress::SciData)==60,"SCI second byte independent of exec partition");
            q.write8(mc68k::PeriphAddress::SciControl1,2);
            require(q.sciFrameCycles()==7392,"SCI 11-bit frame byte write");
            q.write8(mc68k::PeriphAddress::SciControl1,0);
            q.write16(mc68k::PeriphAddress::SciControl0,0);q.writeSciRX(100);advance(10000);
            require(!(q.read16(mc68k::PeriphAddress::SciStatus)&0x40),"Zero divisor stops receiver clock");
        }
        nmm::Flash flash;
        const auto command=[&](uint8_t c) {flash.write(0x555,0xaa);flash.write(0x2aa,0x55);flash.write(0x555,c);};
        flash.write(0x555,0x90);require(flash.read(0)==0xff,"Flash ignores locked commands");
        command(0x90);require(flash.read(0)==1 && flash.read(1)==0xd5,"AMD flash identification");
        flash.write(0,0xf0);
        command(0xa0);flash.write(0x12345,0xf0);require(flash.read(0x12345)==0xf0,"Program F0 as data");
        command(0xa0);flash.write(0x12345,0x0f);require(flash.read(0x12345)==0,"Programming only clears bits");
        command(0xa0);flash.write(0x23456,0x55);
        command(0x80);flash.write(0x555,0xaa);flash.write(0x2aa,0x55);flash.write(0x12345,0x30);
        require(flash.read(0x12345)==0xff && flash.read(0x23456)==0x55,"Sector erase preserves other sectors");
        nmm::Flash restored;restored.restore(flash.data());
        require(restored.read(0x23456)==0x55,"Flash image round trip");
        command(0x80);flash.write(0x555,0xaa);flash.write(0x2aa,0x55);flash.write(0x555,0x10);
        require(flash.read(0x23456)==0xff && restored.read(0x23456)==0x55,"Chip erase and instance isolation");
		nmm::PcPort pc;
		auto writePc=[&](uint8_t address,uint8_t value) {pc.bus(address,value);pc.bus(address&~4,value);pc.bus(7,0);};
		auto readPc=[&](uint8_t address) {pc.bus(address,0);pc.bus(address&~2,0);auto value=pc.data();pc.bus(7,0);return value;};
		writePc(0x76,5); // SCC2691 enable receiver and transmitter
		const uint8_t bytes[]{0xf0,0x33,0,6};
		require(pc.receive(bytes,sizeof bytes),"PC MIDI receive queue");
		pc.advance(319,1000000);require(!pc.interrupt(),"MIDI byte must take 320 us");
		pc.advance(1,1000000);require(pc.takeInterrupt() && !pc.takeInterrupt(),"One interrupt per ready byte");
		require(readPc(0x7e)==0xf0,"GPIO strobes read RX data");
		pc.advance(960,1000000);require((readPc(0x3e)&3)==3,"Three-byte UART FIFO");
		require(pc.receive(bytes,1),"Queue FIFO overflow probe");pc.advance(320,1000000);
		require(readPc(0x3e)&16,"UART overrun status");
		writePc(0x76,0x20);require(!(readPc(0x3e)&19),"Receiver reset clears FIFO and overrun");
		writePc(0x7e,0xf7);require(!(readPc(0x3e)&4),"Transmitter busy until byte sent");
		pc.advance(320,1000000);require(pc.takeOutput()==std::vector<uint8_t>{0xf7},"Timed PC TX output");
        pc.advance(50001,1000000);require(pc.idle(1000000),"UART becomes quiet after traffic");
        writePc(0x7e,0xf0);require(!pc.idle(1000000),"Outgoing reply wakes a quiet UART");
        require(pc.inputIdle(1000000),"Outgoing notifications must not prevent settled-input snapshots");
        pc.advance(320,1000000);require(!pc.idle(1000000),"Quiet interval includes transmit activity");
        pc.advance(50001,1000000);require(pc.idle(1000000),"UART sleeps after complete reply and quiet interval");
        writePc(0x76,1);require(pc.receive(bytes,1),"Snapshot incoming edit probe");
        require(!pc.inputIdle(1000000),"Queued edits prevent snapshots immediately");
        pc.advance(320,1000000);pc.advance(50001,1000000);
        require(!pc.inputIdle(1000000),"Unread FIFO data prevents snapshots even after quiet interval");
        require(readPc(0x7e)==0xf0 && pc.inputIdle(1000000),"Snapshot readiness returns after incoming bytes are consumed and quiet");
		dsp56k::DefaultMemoryValidator validator;
		dsp56k::Peripherals56303 peripherals;
		dsp56k::PeripheralsNop nop;
		dsp56k::Memory memory(validator,0x20000,0x200000,0x200000);
		dsp56k::DSP dsp(memory,&peripherals,&nop);
		auto& timers=peripherals.getTimers();
		timers.setDSP(&dsp,true);timers.setTimerUpdateInterval(1);
		timers.writeTLR(0,0);timers.writeTCSR(0,0x11);
		dsp.fastForward(0,2);timers.exec(); // first tick loads TLR
		for(unsigned i=0;i<100;++i) {dsp.fastForward(0,3);timers.exec();}
		require(timers.readTCR(0)==150,"Timer CLK/2 retains odd-cycle remainder independently of instruction count");
		timers.writeTCSR(0,0);
		timers.writeTLR(0,0xffffab);timers.writeTCSR(0,0x271);timers.writeTCR(0,0xffffab);
		dsp.fastForward(0,2*(85*5+12));timers.exec();
		require(timers.readTCR(0)==0xffffab+12,"PWM timer retains phase across several elapsed periods");
		require(timers.readTCSR(0)&(1u<<dsp56k::Timer::M_TOF),"PWM overflow flag is latched");
		timers.writeTCSR(0,0);timers.setTimerUpdateInterval(2048);
		auto& essi=peripherals.getEssi0();
		auto& dma=peripherals.getDMA();
		essi.setWriteTxCallback([](uint64_t&,const dsp56k::Audio::TxFrame&){});
		essi.writeCRA(0x181808); // two slots, 24-bit words
		essi.writeTSMA(1); // slot 1 is disabled
		essi.writeCRB(0x12000); // network mode, TX0 enabled
		memory.set(dsp56k::MemArea_Y,0x100,0x12345);
		memory.set(dsp56k::MemArea_Y,0x101,0x23456);
		dma.setDSR(4,0x100);dma.setDDR(4,0xffffbc);dma.setDCO(4,1);
		dma.setDCR(4,0x885a51);
		require(dma.getDSR(4)==0x101,"Pending ESSI TDE must be serviced when DMA is enabled");
		require(!essi.getSR().test(dsp56k::Essi::SSISR_TDE),"DMA write acknowledges TDE");
		essi.execTX(); // disabled slot 1
		require(dma.getDSR(4)==0x101,"Masked TX slot must not consume DMA data");
		require(!essi.getSR().test(dsp56k::Essi::SSISR_TDE),"Masked TX slot must not set TDE");
		essi.execTX(); // enabled slot 0
		require(dma.getDSR(4)==0x102 && !(dma.getDCR(4)&0x800000),"Next enabled slot completes DMA block");
        auto& receive=peripherals.getEssi1();
        receive.setReadRxCallback([](uint64_t& index,dsp56k::Audio::RxFrame& f) {f.resize(2);f[0].fill(0);f[1].fill(0);f[0][0]=0x123400+index++;});
        receive.writeCRA(0x181808);receive.writeRSMA(1);receive.writeCRB(0x22000);
        dma.setDSR(3,0xffffa8);dma.setDDR(3,0x180);dma.setDCO(3,0);dma.setDCR(3,0xa86240);
        receive.execRX();require(memory.get(dsp56k::MemArea_X,0x180)==0x123400,"Fixed-address DMA receives first word");
        receive.execRX();require(memory.get(dsp56k::MemArea_X,0x180)==0x123400,"Masked receive slot preserves latch");
        receive.execRX();require(memory.get(dsp56k::MemArea_X,0x180)==0x123401,"Fixed-address DMA repeats with DE retained");
        require(dma.getDDR(3)==0x180 && dma.getDSR(3)==0xffffa8 && (dma.getDCR(3)&0x800000),"Repeated DMA preserves both addresses");
        dma.setDCR(3,0);receive.writeCRB(0);
		// Firmware DMA0 clears 18 mix words and rewinds both addresses by 17.
		for(unsigned i=0;i<18;++i) memory.set(dsp56k::MemArea_X,0x200+i,i+1);
		dma.setDOR(3,0xffffef);
		dma.setDSR(0,0x200);dma.setDDR(0,0x300);dma.setDCO(0,17);dma.setDCR(0,0x9801b4);
		dma.setDSR(4,0x100);dma.setDCO(4,0);dma.setDCR(4,0x885a51);
		dma.trigger(dsp56k::DmaChannel::RequestSource::Essi0TransmitData);
		require(dma.getDSTR()&(1u<<dsp56k::Dma::Dact),"TX completion must preserve pending buffer copy");
		for(unsigned i=0;i<128;++i) {dsp.execInterpreter();dma.exec();}
		for(unsigned i=0;i<18;++i) require(memory.get(dsp56k::MemArea_Y,0x300+i)==i+1,"Dual-counter block copies every mix word");
		require(dma.getDSR(0)==0x200 && dma.getDDR(0)==0x300 && !(dma.getDCR(0)&0x800000),"Both DMA addresses rewind on completion");
        // Core-clock timing is opt-in. A two-word DMA block takes four cycles,
        // independent of the interpreter/JIT instruction count.
        dma.setUseCycles(true);
        memory.set(dsp56k::MemArea_X,0x400,0x765432);memory.set(dsp56k::MemArea_X,0x401,0x654321);
        dma.setDSR(1,0x400);dma.setDDR(1,0x500);dma.setDCO(1,1);dma.setDCR(1,0x9802d0);
        dsp.fastForward(0,3);dma.exec();require(memory.get(dsp56k::MemArea_X,0x500)==0,"DMA does not complete before core-cycle deadline");
        dsp.fastForward(0,1);dma.exec();require(memory.get(dsp56k::MemArea_X,0x500)==0x765432 && memory.get(dsp56k::MemArea_X,0x501)==0x654321,"DMA completes at core-cycle deadline");
		std::cout<<"PASS PC UART, cycle timer, ESSI/DMA: pending request, masked slots, completion, dual-counter mix buffer copy\n";
		return 0;
	}
	catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
