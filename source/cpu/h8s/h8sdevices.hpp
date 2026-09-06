#pragma once
#include "h8s.hpp"
#include <functional>
#include <queue>

// CatchAll logs unconditionally to alert you to bad read/writes.
class CatchAllDevice : public H8SDevice
{
public:
	CatchAllDevice() {}
	virtual uint8_t read(uint32_t address) { printf("Uncaught read 0x%06x\n",address); return 0;}
	virtual void write(uint32_t address, uint8_t value) { printf("Uncaught write 0x%06x, 0x%02x\n",address,value&255);}
};

// H8S timers (all timers)
class Timers : public H8SDevice
{
public:
	Timers() {}
	void tick_extclock(int which) {
		channels[which].gra++;
	}
	void tick()
	{
		for (int i = 0; i < 5; i++)
		{
			if (!(tstr & (1 << i))) continue;	// skip halted channels
			if (tmdr & (1 << i)) continue;		// skip pwm channels
			class channel& c = channels[i];
			if (c.tcr & 4) continue;			// skip externally clocked channels
			int shift = (c.tcr & 3);
			unsigned int inc = (unsigned int)((state->cycles >> shift) - (lastCycles >> shift));	// how many cycles have passed?
			for (int j = 0; j < inc; j++)
			{
				uint16 nt = c.tcnt + 1;
				if (c.tcnt == c.gra)
				{
					c.tsr |= 1;
					if ((c.tcr & 0x60) == 0x20) nt = 0;
					if (c.tier & 1) state->interrupt(24 + i * 4);
				}
				if (c.tcnt == c.grb)
				{
					c.tsr |= 2;
					if ((c.tcr & 0x60) == 0x40) nt = 0;
					if (c.tier & 2) state->interrupt(25 + i * 4);
				}
				if (!c.tcnt)
				{
					c.tsr |= 4;
					if (c.tier & 4) state->interrupt(26 + i * 4);
				}
				c.tcnt = nt;
			}
		}
		lastCycles = state->cycles;
	}
	virtual uint8_t read(uint32_t address) {
		if (address<0xffff60 || address >= 0xffffa0) return 0;
		address -= 0xffff60;
		switch (address)
		{
			case 0: return tstr;
			case 1: return tsnc;
			case 2: return tmdr;
			case 3: return tfcr;
			default: break;
		}
		int channel = -1;
		if (address >= 0x4 && address < 0xe) {channel = 0; address -= 0x4;}
		else if (address >= 0xe && address < 0x18) {channel = 1; address -= 0xe;}
		else if (address >= 0x18 && address < 0x22) {channel = 2; address -= 0x18;}
		else if (address >= 0x22 && address < 0x2c) {channel = 3; address -= 0x22;}
		else if (address >= 0x32 && address < 0x3c)	{channel = 4; address -= 0x32;}
		if (channel == -1) return space[address];
		
		const class channel& c = channels[channel];
		switch (address)
		{
			case 0: return c.tcr;
			case 1: return 0;	// tior
			case 2: return c.tier;
			case 3: return c.tsr;
			case 4: return ((c.tcnt) >> 8) & 255;
			case 5: return c.tcnt & 255;
			case 6: return ((c.gra) >> 8) & 255;
			case 7: return c.gra & 255;
			case 8: return ((c.grb) >> 8) & 255;
			case 9: return c.grb & 255;
			default: break;
		}
		return 0;
	}
		
	virtual void write(uint32_t address, uint8_t value) {
		if (address<0xffff60 || address >= 0xffffa0) return;
		address -= 0xffff60;
		switch (address)
		{
			case 0: tstr = value; return;
			case 1: tsnc = value; return;
			case 2: tmdr = value; return;
			case 3: tfcr = value; return;
			default: break;
		}
		int channel = -1;
		if (address >= 0x4 && address < 0xe) {channel = 0; address -= 0x4;}
		else if (address >= 0xe && address < 0x18) {channel = 1; address -= 0xe;}
		else if (address >= 0x18 && address < 0x22) {channel = 2; address -= 0x18;}
		else if (address >= 0x22 && address < 0x2c) {channel = 3; address -= 0x22;}
		else if (address >= 0x32 && address < 0x3c)	{channel = 4; address -= 0x32;}
		if (channel == -1) {space[address] = value; return;}
		
		class channel& c = channels[channel];
		switch (address)
		{
			case 0: c.tcr = value; return;
			case 1: return;	// tior
			case 2: c.tier = value; return;
			case 3: c.tsr = value; return;
			case 4: c.tcnt = (c.tcnt & 255) | (((int)value) << 8); return;
			case 5: c.tcnt = (c.tcnt & 0xff00) | (value & 0xff); return;
			case 6: c.gra = (c.gra & 255) | (((int)value) << 8); return;
			case 7: c.gra = (c.gra & 0xff00) | (value & 0xff); return;
			case 8: c.grb = (c.grb & 255) | (((int)value) << 8); return;
			case 9: c.grb = (c.grb & 0xff00) | (value & 0xff); return;
			default: break;
		}
	}

private:	// 0x9f -> 0x60
	unsigned long long lastCycles {0};
	int8 space[64] {};
	uint8 tstr {0xc0}, tsnc {0xc0}, tmdr {0x80}, tfcr {0xc0};	// timer start, timer sync, timer mode reg, timer function control
	class channel
	{
	public:
		uint16 tcnt {0}, gra {0xffff}, grb {0xffff};
		uint8 tcr {128}, tsr {0xf8}, tier {0xf8};
	};
	channel channels[5];
};

class RefreshController : public H8SDevice
{
public:
	virtual uint8_t read(uint32_t address)
	{
		address &= 3;
		int force = 0;
		if (address == 0) force = 2;
		if (address == 1) force = 7;
		return regs[address] | force;	
	}
	virtual void write(uint32_t address, uint8_t value)
	{
		address &= 3;
		if (address == 0) printf("Write to RFSHCR %02x\n", value);
		if (address == 1)
		{
			value &= 0x7f;
			const int shifts[8] = {0, 1, 3, 5, 7, 9, 11, 12};
			shift = shifts[(value >> 3) & 7];
			cmie = value & 64;
		}
		regs[address] = value;
	}
	void tick()
	{
		if (!shift) return;
		unsigned int inc = (unsigned int)((state->cycles >> shift) - (lastCycles >> shift));	// how many cycles have passed?
		for (int i = 0; i < inc; i++)
		{
			regs[2]++;
			if (regs[2] != regs[3]) continue;
			regs[2] = 0;
			regs[0] |= 128; // Set CMF
			if (cmie) state->interrupt(21);
		}
		lastCycles = state->cycles;
		
	}
protected:
	uint8_t regs[4] {2, 7, 0, 255};
	uint64_t lastCycles {0};
	uint32_t shift {0};
	bool cmie {false};
};

// H8S Serial port (single)
class Serial : public H8SDevice
{
public:
	Serial(int _irqoff = 0, std::function<void(uint8_t)>&& _outputCallback = [](uint8_t) {}) : irqoff(_irqoff), outputCallback(std::move(_outputCallback)) {}
	virtual uint8_t read(uint32_t address) {
		address &= 7;
		switch (address)
		{
			case 2: return scr;
			case 3: return txr;
			case 4: return ssr;
			case 5: return rdr;
			default:
				return data[address];
		}
	}
	virtual void write(uint32_t address, uint8_t value) {
		address &= 7;
		switch (address)
		{
			case 2:
				if ((value & 32) && !(scr & 32)) txrtimer = 1;
				if (!(value & 32) && (scr & 32)) txrtimer = 0;
				scr = value;
				break;
			case 3:
				txr = value;
				if (value != 0xfe)
				{
					printf("MIDI Out: [%02x]\n", value & 255);
					outputCallback(static_cast<uint8_t>(value & 255));
				}
				txrtimer = clocktime;
				ssr &= 0x7b;
				break;
			case 4: ssr = value; break;
			case 5:	break;
			default:
				data[address] = value;
				break;
		}
	}
	void provideMIDI(const uint8 *data, size_t len) {for (size_t i = 0; i < len; i++) tosend.push(data[i]);}
	void tick()
	{
		if (txrtimer)
		{
			unsigned long long cycles = state->getCycles();
			int diff = (int)(cycles - lastcycles);
			lastcycles = cycles;
			txrtimer -= diff;
			if (txrtimer <= 0)
			{
				txrtimer = clocktime;
				if (scr & 128) state->interrupt(54 + irqoff);
				if (scr & 4) state->interrupt(55 + irqoff);
				ssr |= 128 | 4;
			}
		}
		if (!tosend.empty() && !(ssr & 64))
		{
			ssr |= 64;
			rdr = tosend.front();
			tosend.pop();
			state->interrupt(53 + irqoff);
		}
	}
protected:
	static constexpr int clocktime = (16000000 / 31250) * 10;	// 5120 clocks to send a midi byte.
	std::queue<uint8> tosend;
	int8 data[8], scr {0}, txr {-1}, rdr {0}, ssr {-128};
	unsigned long long lastcycles {0};
	int irqoff {0}, txrtimer {0};
	std::function<void(uint8_t)> outputCallback;
};

class MIDISerial : public Serial
{
public:
	MIDISerial(int _irqoff = 0, std::function<void(uint8_t)>&& _outputCallback = [](uint8_t) {}) : Serial(_irqoff), outputCallback(std::move(_outputCallback)) {}

	virtual void write(uint32_t address, uint8_t value) {
		address &= 7;
		Serial::write(address, value);
		if (address == 3 && value != 0xfe)
		{
			printf("MIDI Out: [%02x]\n", value & 255);
			outputCallback(static_cast<uint8_t>(value & 255));
		}
	}
	void provideMIDI(const uint8 *data, size_t len) {for (size_t i = 0; i < len; i++) tosend.push(data[i]);}

protected:
	std::function<void(uint8_t)> outputCallback;
};

class ISR : public H8SDevice
{
public:
	virtual void write(uint32_t address, uint8_t value) {
		state->pending_irqs &= 0xfffffffffffc0fffull | (((uint32_t)value & 0x3f) << 12);	
	}
	virtual uint8_t read(uint32_t address) { return (state->pending_irqs >> 12) & 0x3f; }
};

class IER : public H8SDevice
{
public:
	virtual void write(uint32_t address, uint8_t value) { val = value; }
	virtual uint8_t read(uint32_t address) { return val; }
protected:
	uint8_t val {0};
};

class ADC : public H8SDevice
{
public:
	ADC(uint16_t value) : val(value) {}
	virtual uint8_t read(uint32_t address) {
		return (address & 1) ? (val & 255) : (val >> 8);
	}
	virtual void write(uint32_t address, uint8_t value) {
	}
protected:
	uint16_t val;
};

class DMA : public H8SDevice
{
public:
	DMA(int _irq_off = 0) : irq_off(_irq_off) {}
// regs:
// MARAR, MARAE, MARAH, MARAL, ETCRAH, ETCRAL, IOARA, DTCRA
// MARBR, MARBE, MARBH, MARBL, ETCRBH, ETCRBL, IOARB, DTCRB

	virtual uint8_t read(uint32_t address)
	{
		address &= 15;
		return regs[address];
	}
	virtual void write(uint32_t address, uint8_t value)
	{
		address &= 15;
		regs[address] = value;
		
		if (address == 7) // DTCRA
		{
			int mode = value & 7;
			if (!(value & 128)) return; // enabled
			int size = (value & 64) ? 2 : 1; // byte/word

			assert((mode == 6) && (size == 1)); // full address mode. normal transfer
			int saia = (value >> 4) & 3;
			int inca = (saia == 1) ? 1 : (saia == 3) ? -1 : 0;
		
			int valb = regs[15]; // DTCRB
			if (!(valb & 128)) return; // b is disabled
			int saib = (valb >> 4) & 3;
			int incb = (saib == 1) ? 1 : (saib == 3) ? -1 : 0;
			assert((valb & 7) == 0); // auto-request
			
			uint32_t from = (((uint32_t)regs[1]) << 16) | (((uint32_t)regs[2]) << 8) | regs[3];
			uint32_t to = (((uint32_t)regs[9]) << 16) | (((uint32_t)regs[10]) << 8) | regs[11];
			uint32_t count = (((uint32_t)regs[4]) << 8) | regs[5];
			// printf("DMA transfer from %06x to %06x, count %d, inca %d, incb %d\n", from, to, count, inca, incb);
			for (int i = 0; i < count; i++)
			{
				state->write8(state->read8(from), to);
				from += inca;
				to += incb;
			}
			if (value & 8) state->interrupt(44 + irq_off);
			if (valb & 8) state->interrupt(45 + irq_off);	
		}
	}	
protected:
	uint8_t regs[16] {}, irq_off {0};
};

class HWRegs : public H8SDevice
{
public:
	HWRegs() {}
	virtual uint8_t read(uint32_t address) { return data[address&255]; }
	virtual void write(uint32_t address, uint8_t value) { data[address&255] = value; }
protected:
	int8 data[256];
};
