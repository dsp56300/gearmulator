#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "cfCpu.h"

namespace coldfire
{
	// Something on the external bus of the MCF5206e, a chip select or a DRAM bank. Offsets are relative to
	// the start of the window. Accesses arrive already split to the port size of the window.
	class Device
	{
	public:
		virtual ~Device() = default;

		virtual uint32_t read(uint32_t _offset, uint32_t _size) = 0;
		virtual void write(uint32_t _offset, uint32_t _value, uint32_t _size) = 0;

		// plain memory is accessed directly, without the virtual calls above
		virtual uint8_t* getMemory() { return nullptr; }
		virtual uint32_t getMemorySize() const { return 0; }
	};

	// Byte addressed big endian RAM
	class Ram final : public Device
	{
	public:
		explicit Ram(const uint32_t _size) : m_data(_size, 0) {}

		uint32_t read(uint32_t _offset, uint32_t _size) override;
		void write(uint32_t _offset, uint32_t _value, uint32_t _size) override;

		uint8_t* getMemory() override { return m_data.data(); }
		uint32_t getMemorySize() const override { return static_cast<uint32_t>(m_data.size()); }

		std::vector<uint8_t>& data() { return m_data; }

	private:
		std::vector<uint8_t> m_data;
	};

	// MCF5206e UART (UM section 12), a 68681 style channel with a three byte receive FIFO
	class Uart
	{
	public:
		using TxCallback = std::function<void(uint8_t)>;

		void reset();

		uint8_t read(uint32_t _reg);
		void write(uint32_t _reg, uint8_t _value);

		// advances the transmitter and receiver by system clock cycles
		void advance(uint32_t _cycles);

		// external side: bytes that arrive on RxD; they enter the FIFO at the programmed baud rate
		void receive(const uint8_t _byte)
		{
			m_rxPending.push_back(_byte);
			if(m_wakeup)
				*m_wakeup = 0;
		}
		bool isReceiveQueueEmpty() const { return m_rxPending.empty(); }

		void setTxCallback(TxCallback _cb) { m_txCallback = std::move(_cb); }

		// state of the RTS output (UOP bit 0), true = asserted (pin low)
		bool getRts() const { return (m_outputPort & 1) != 0; }

		bool interruptRequest() const { return (isr() & m_imr) != 0; }
		uint8_t getVector() const { return m_ivr; }

		void setSystemClock(const uint32_t _hz) { m_systemClock = _hz; }

		// cycles until advance() changes something on its own, UINT32_MAX if nothing is under way
		uint32_t cyclesToNextEvent() const;

		// the owner's event countdown, zeroed when a byte arrives from outside so that it is seen at once
		void setWakeup(uint32_t* _countdown) { m_wakeup = _countdown; }

	private:
		uint8_t isr() const;
		uint8_t usr() const;
		uint32_t cyclesPerCharacter() const;

		uint32_t m_systemClock = 40000000;

		uint8_t m_mr1 = 0;
		uint8_t m_mr2 = 0;
		bool m_mrPointer2 = false;
		uint8_t m_csr = 0xdd;
		uint8_t m_imr = 0;
		uint8_t m_acr = 0;
		uint8_t m_bg1 = 0;
		uint8_t m_bg2 = 0;
		uint8_t m_ivr = 0x0f;
		uint8_t m_outputPort = 0;

		bool m_txEnabled = false;
		bool m_rxEnabled = false;

		// transmitter: holding register and shift register
		bool m_txHoldingFull = false;
		uint8_t m_txHolding = 0;
		bool m_txShifting = false;
		uint8_t m_txShift = 0;
		uint32_t m_txRemaining = 0;

		// receiver: FIFO, overrun, bytes waiting on the line and the time until the next one completes
		std::deque<uint8_t> m_rxFifo;
		bool m_overrun = false;
		std::deque<uint8_t> m_rxPending;
		uint32_t m_rxRemaining = 0;

		TxCallback m_txCallback;
		uint32_t* m_wakeup = nullptr;
	};

	// MCF5206e general purpose timer (UM section 14)
	class Timer
	{
	public:
		void reset();

		uint16_t read16(uint32_t _reg) const;
		uint8_t read8(uint32_t _reg) const;
		void write16(uint32_t _reg, uint16_t _value);
		void write8(uint32_t _reg, uint8_t _value);

		void advance(uint32_t _cycles);

		// cycles until the counter reaches the reference value, UINT32_MAX while stopped
		uint32_t cyclesToNextEvent() const;

		bool interruptRequest() const;

	private:
		// system clock cycles per count, 0 while stopped
		uint32_t divider() const;

		uint16_t m_tmr = 0;
		uint16_t m_trr = 0xffff;
		uint16_t m_tcr = 0;
		uint16_t m_tcn = 0;
		uint8_t m_ter = 0;
		uint32_t m_prescale = 0;
	};

	// The MCF5206e: ColdFire V2 core with SIM, interrupt controller, chip selects, DRAM controller,
	// two UARTs, two timers, the software watchdog, 8 KB SRAM and the parallel port.
	class Mcf5206e final : public Bus
	{
	public:
		static constexpr uint32_t SramSize = 8192;

		enum class Region : uint8_t
		{
			ChipSelect0, ChipSelect1, ChipSelect2, ChipSelect3, ChipSelect4, ChipSelect5, ChipSelect6, ChipSelect7,
			Dram0, Dram1,
			Count
		};

		using ResetCallback = std::function<void()>;
		using ModuleWriteMonitor = std::function<void(uint32_t _offset, uint32_t _value, uint32_t _size)>;

		explicit Mcf5206e(uint32_t _systemClockHz = 40000000);

		void attach(Region _region, Device* _device);

		// hardware reset: core, SIM and modules to their reset values, then the reset exception
		void reset();

		// executes one instruction (or takes an exception) and advances the modules, returns the cycles used
		uint32_t exec();

		Cpu& getCpu() { return m_cpu; }
		Uart& getUart(const uint32_t _index) { return m_uarts[_index]; }

		// external interrupt pins IRQ1, IRQ4, IRQ7 (individual interrupt mode), true = asserted
		void setExternalInterrupt(uint32_t _level, bool _asserted);

		// parallel port pins driven from outside
		void setParallelPortInput(const uint8_t _value) { m_ppInput = _value; }
		uint8_t getParallelPortOutput() const { return m_ppdat; }

		// the software watchdog resets the chip; the callback lets the owner reset the board with it
		void setWatchdogResetCallback(ResetCallback _cb) { m_watchdogReset = std::move(_cb); }

		// sees every write the core makes to the module registers, for bring-up
		void setModuleWriteMonitor(ModuleWriteMonitor _cb) { m_moduleWriteMonitor = std::move(_cb); }

		// sees every interrupt acknowledge: level and the vector that answered
		void setInterruptMonitor(std::function<void(uint8_t _level, uint8_t _vector)> _cb) { m_interruptMonitor = std::move(_cb); }

		// state after a boot program ran, for starting code without one
		void setMbar(uint32_t _value);
		void setRambar(uint32_t _value);
		void writeRegister8(uint32_t _offset, uint8_t _value) { writeModule(_offset, _value, 1); m_nextPeripheralEvent = 0; }
		void writeRegister16(uint32_t _offset, uint16_t _value) { writeModule(_offset, _value, 2); m_nextPeripheralEvent = 0; }
		void writeRegister32(uint32_t _offset, uint32_t _value) { writeModule(_offset, _value, 4); m_nextPeripheralEvent = 0; }

		// Bus
		uint8_t read8(uint32_t _addr) override { return static_cast<uint8_t>(access(_addr, 0, 1, false)); }
		uint16_t read16(uint32_t _addr) override { return static_cast<uint16_t>(access(_addr, 0, 2, false)); }
		uint32_t read32(uint32_t _addr) override { return access(_addr, 0, 4, false); }
		void write8(uint32_t _addr, uint8_t _value) override { access(_addr, _value, 1, true); }
		void write16(uint32_t _addr, uint16_t _value) override { access(_addr, _value, 2, true); }
		void write32(uint32_t _addr, uint32_t _value) override { access(_addr, _value, 4, true); }
		uint16_t fetch16(uint32_t _addr) override;
		uint8_t interruptAcknowledge(uint8_t _level) override;
		void writeControlRegister(uint16_t _reg, uint32_t _value) override;
		uint32_t consumeWaitCycles() override { const auto c = m_waitCycles; m_waitCycles = 0; return c; }

		// debugger style access that does not touch peripherals with side effects
		uint8_t peek8(uint32_t _addr);

	private:
		struct Page
		{
			Device* device = nullptr;
			uint8_t* memory = nullptr;	// direct access if the device is plain memory
			uint32_t base = 0;			// address of the first byte of the window
			uint32_t memoryMask = 0;
			uint8_t portSize = 4;
			uint8_t waitCycles = 0;
			bool read = false;
			bool write = false;
		};

		uint32_t access(uint32_t _addr, uint32_t _value, uint32_t _size, bool _write);
		uint32_t accessExternal(uint32_t _addr, uint32_t _value, uint32_t _size, bool _write);

		uint32_t readModule(uint32_t _offset, uint32_t _size);
		void writeModule(uint32_t _offset, uint32_t _value, uint32_t _size);
		uint8_t readModuleByte(uint32_t _offset);
		void writeModuleByte(uint32_t _offset, uint8_t _value);

		void rebuildMemoryMap();
		void syncPeripherals();
		void updateInterrupts();
		uint8_t acknowledgeInterrupt(uint8_t _level);
		uint32_t watchdogTimeout() const;
		void advanceWatchdog(uint32_t _cycles);
		void watchdogReset();
		void resetModules();

		Cpu m_cpu;

		uint32_t m_systemClock;

		// MOVEC registers
		uint32_t m_mbar = 0;
		uint32_t m_rambar = 0;
		uint32_t m_cacr = 0;
		uint32_t m_acr0 = 0;
		uint32_t m_acr1 = 0;

		std::array<uint8_t, SramSize> m_sram{};

		// SIM
		uint8_t m_simr = 0xc0;
		std::array<uint8_t, 16> m_icr{};
		uint16_t m_imr = 0x3ffe;
		uint16_t m_ipr = 0;
		uint8_t m_rsr = 0x80;
		uint8_t m_sypcr = 0;
		bool m_sypcrWritten = false;
		uint8_t m_swivr = 0x0f;
		uint8_t m_swsrLast = 0;
		uint16_t m_par = 0;
		uint32_t m_watchdogCounter = 0;
		uint16_t m_externalIrq = 0;	// bit per level 1-7

		// UARTs, timers and the watchdog run behind the core and catch up when one of them has an event
		// due or the core touches a module register, so interrupts arrive at the same instruction as if
		// they were advanced after every instruction
		uint32_t m_peripheralCycles = 0;		// cycles the core ran since the last catch up
		uint32_t m_nextPeripheralEvent = 0;		// catch up once m_peripheralCycles reaches this

		// chip selects, DRAM controller, default memory
		std::array<uint16_t, 8> m_csar{};
		std::array<uint32_t, 8> m_csmr{};
		std::array<uint16_t, 8> m_cscr{};
		bool m_globalChipSelect = true;
		uint16_t m_dmcr = 0;
		uint16_t m_dcrr = 0;
		uint16_t m_dctr = 0;
		std::array<uint16_t, 2> m_dcar{};
		std::array<uint32_t, 2> m_dcmr{};
		std::array<uint8_t, 2> m_dccr{};

		// parallel port
		uint8_t m_ppddr = 0;
		uint8_t m_ppdat = 0;
		uint8_t m_ppInput = 0xff;

		// MBUS and DMA registers are stored but the modules are not modelled
		std::array<uint8_t, 0x20> m_mbus{};
		std::array<uint8_t, 0x80> m_dma{};

		std::array<Uart, 2> m_uarts;
		std::array<Timer, 2> m_timers;

		std::array<Device*, static_cast<size_t>(Region::Count)> m_devices{};
		std::vector<Page> m_pages;	// one entry per 64 KB
		uint32_t m_waitCycles = 0;

		ResetCallback m_watchdogReset;
		ModuleWriteMonitor m_moduleWriteMonitor;
		std::function<void(uint8_t, uint8_t)> m_interruptMonitor;
	};
}
