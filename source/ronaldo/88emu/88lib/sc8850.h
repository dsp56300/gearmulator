#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include "cpu/sh2/machine.hpp"
#include "custom_chips/lsp/lsp.h"
#include "custom_chips/xp/xp.h"
#include "hardwareLib/sed1335.h"
#include "hardwareLib/tc160g22af.h"
#include "sc8850_submcu.h"

namespace emu88Lib
{
	// SC-8850 front-panel inputs in service-manual order. SW1..SW19 are the
	// diode matrix. PREVIEW and VALUE PUSH enter the same gate-array key stream
	// through its two direct switch pins (VOLSW and ENCSW), so they occupy the
	// next two bitmap positions even though they have no SW number.
	enum class Sc8850Button : uint8_t
	{
		Edit = 0,
		PartLeft,
		PartRight,
		Drum,
		Variation,
		Instrument,
		Effects,
		Exit,
		Enter,
		Shift,
		Solo,
		Mute,
		Dec,
		Inc,
		F1,
		F2,
		F3,
		F4,
		InstMap,
		ValuePush,
		PreviewPush,
		Count
	};

	constexpr uint32_t sc8850ButtonBit(const Sc8850Button _button) { return 1u << static_cast<uint8_t>(_button); }

	// Roland SC-8850 mainboard.
	//
	// SH7016 (28.224 MHz, on-chip 64 KiB boot ROM) on the cached threaded-code
	// sh2::Machine, 1 MiB program flash on CS0, 2 MiB data flash on CS3,
	// 512 KiB work RAM in DRAM space, two XP tone generators, the LSP effects
	// processor, the TC160G22AF gate array (interrupt multiplexer, panel,
	// timers), the M37640 USB controller and a SED1335-compatible 160x64
	// display.  Memory map (24-bit external space):
	//
	//   00000000-0000FFFF  on-chip ROM            00500000/1  SED1335 data / command
	//   00200000-003FFFFF  program flash (1 MB x2) 00540000/1  USB channel 0 (host -> MCU)
	//   00400000-007FFFFF  CS1 RAM                 00580000/1  USB channel 1 (MCU -> host)
	//   00800000-00BFFFFF  CS2 RAM                 005C0000-F  LSP host registers
	//   00C00000-00CFFFFF  CS3 RAM                 006C0000-7F gate array
	//   00D00000-00EFFFFF  data flash              00A00000-3FFF  XP0
	//   00F00000-00FFFFFF  CS3 RAM (shadow)        00A80000-3FFF  XP1
	//   01000000-0107FFFF  work RAM (DRAM)
	//
	// Interrupts: XP0INT -> IRQ0, XP1INT -> IRQ1, gate array -> IRQ2.  The
	// display frame buffer reaches the SED1335 by DMA: channel 1 copies it to
	// the data port one byte per DREQ1 pulse, which the board supplies at the
	// LCD interface rate while the channel is enabled.
	class Sc8850
	{
	public:
		enum class MidiTransport : uint8_t
		{
			Usb,
			Sci
		};

		using SampleFrame = std::pair<int32_t, int32_t>;
		using Lsp = lspLib::LSPDispatcher;

		static constexpr uint32_t CpuRomSize = 0x10000;
		static constexpr uint32_t ProgramRomSize = 0x100000;
		static constexpr uint32_t ProgramEraseBlockSize = 0x10000;
		static constexpr uint32_t DataRomSize = 0x200000;
		static constexpr uint32_t WorkRamSize = 0x080000;
		static constexpr uint32_t LcdWidth = 160;
		static constexpr uint32_t LcdHeight = 64;
		static constexpr uint32_t LcdVramSize = 0x10000;
		static constexpr uint32_t CpuClockHz = 28224000;
		static constexpr uint32_t SampleRate = 32000;
		static constexpr uint32_t StatesPerSample = CpuClockHz / SampleRate; // 882
		static constexpr uint32_t VoiceControlRate = SampleRate / 256;
		static constexpr uint32_t RomPlayControlRate = SampleRate / 32;
		static constexpr uint32_t ProgramFlashBase = 0x00200000;
		static constexpr uint32_t WorkRamBase = 0x01000000;
		// States between two DREQ1 pulses while the display DMA runs.
		static constexpr uint64_t LcdDmaByteStates = 16;

		Sc8850(std::vector<uint8_t> _cpuRom, std::vector<uint8_t> _programRom,
			   std::vector<uint8_t> _dataRom, const std::vector<uint8_t>& _waveRom0 = {},
			   const std::vector<uint8_t>& _waveRom1 = {}, bool _factoryReset = true);
		~Sc8850();

		Sc8850(const Sc8850&) = delete;
		Sc8850& operator=(const Sc8850&) = delete;

		void reset();

		bool isValid() const { return m_valid; }
		SampleFrame renderSample();

		hwLib::SED1335& lcd() { return m_lcd; }
		const hwLib::SED1335& lcd() const { return m_lcd; }
		hwLib::Tc160g22af& gateArray() { return m_gateArray; }
		const hwLib::Tc160g22af& gateArray() const { return m_gateArray; }
		void setButton(uint32_t _switchIndex, bool _pressed);
		void setButtons(uint32_t _bitmap);
		void turnEncoder(int8_t _delta);
		// Front-panel LEDs, active high, in Sc8850Led bit order. The panel ASIC
		// has no LED matrix: the firmware drives the six switch LEDs through
		// the gate array's otherwise unused LCD port, active low - EDIT, DRUM
		// and EFFECTS on the data register (0x39) bits 0-2, SHIFT, SOLO and
		// MUTE on the command register (0x38). Zero until the firmware has
		// initialised the port, so a fresh board does not flash every LED.
		enum class Led : uint8_t { Edit = 0, Drum, Effects, Shift, Solo, Mute, Count };
		uint8_t leds() const;
		void setMidiTransport(MidiTransport _transport);
		MidiTransport midiTransport() const { return m_midiTransport; }
		void midiIn(const uint8_t _port, const uint8_t _value) { sciMidiIn(_port, _value); }
		void usbMidiIn(const uint8_t* _data, const size_t _size) { m_usb.midiIn(_data, _size); }
		// USB exposes four virtual cables, one per part group A-D.
		void usbMidiIn(const uint8_t _port, const uint8_t* _data, const size_t _size) { m_usb.midiIn(_port, _data, _size); }
		size_t midiInBacklog() const { return m_usb.inputBacklog(); }
		void sciMidiIn(uint8_t _port, uint8_t _value);
		void readMidiOut(std::vector<uint8_t>& _output);
		void readPhysicalMidiOut(std::vector<uint8_t>& _output);

		uint64_t cycles() const { return m_machine.now(); }

	private:
		enum class FlashMode : uint8_t
		{
			ReadArray,
			ReadId,
			ReadStatus,
			EraseSetup,
			ProgramSetup,
			LockSetup
		};

		// Bus devices decoded inside the external chip-select windows.
		struct FlashDevice final : emu::Device
		{
			explicit FlashDevice(Sc8850& _b) : board(_b) {}
			uint8_t read8(uint32_t _a) override { return board.programFlashRead(_a); }
			void write8(uint32_t _a, uint8_t _v) override { board.programFlashWrite(_a, _v); }
			Sc8850& board;
		};
		struct LcdDevice final : emu::Device
		{
			explicit LcdDevice(Sc8850& _b) : board(_b) {}
			uint8_t read8(uint32_t _a) override { return (_a & 1) ? uint8_t(0) : board.m_lcd.readData(); }
			void write8(uint32_t _a, uint8_t _v) override
			{
				if(_a & 1) board.m_lcd.writeCommand(_v); else board.m_lcd.writeData(_v);
			}
			Sc8850& board;
		};
		struct UsbDevice final : emu::Device
		{
			explicit UsbDevice(Sc8850& _b) : board(_b) {}
			uint8_t read8(uint32_t _a) override { return board.m_usb.hostRead(_a); }
			void write8(uint32_t _a, uint8_t _v) override { board.m_usb.hostWrite(_a, _v); }
			Sc8850& board;
		};
		struct LspDevice final : emu::Device
		{
			explicit LspDevice(Sc8850& _b) : board(_b) {}
			uint8_t read8(uint32_t _a) override { return board.m_lsp.hostRead(uint16_t(_a & 0xf)); }
			void write8(uint32_t _a, uint8_t _v) override { board.m_lsp.hostWrite(uint16_t(_a & 0xf), _v); }
			Sc8850& board;
		};
		struct GaDevice final : emu::Device
		{
			explicit GaDevice(Sc8850& _b) : board(_b) {}
			uint8_t read8(uint32_t _a) override { return board.gateArrayRead(uint8_t(_a)); }
			void write8(uint32_t _a, uint8_t _v) override { board.gateArrayWrite(uint8_t(_a), _v); }
			Sc8850& board;
		};
		struct XpDevice final : emu::Device
		{
			XpDevice(Sc8850& _b, xpLib::XP& _x) : board(_b), xp(_x) {}
			uint8_t read8(uint32_t _a) override { return xp.hostRead8(uint16_t(_a)); }
			void write8(uint32_t _a, uint8_t _v) override { xp.hostWrite8(uint16_t(_a), _v); }
			uint16_t read16(uint32_t _a) override
			{
				const auto v = xp.hostRead(uint16_t(_a));
				return v;
			}
			void write16(uint32_t _a, uint16_t _v) override
			{
				xp.hostWrite(uint16_t(_a), _v);
			}
			Sc8850& board;
			xpLib::XP& xp;
		};

		uint8_t gateArrayRead(uint8_t _reg);
		void gateArrayWrite(uint8_t _reg, uint8_t _val);
		bool queueGateIrq(uint8_t _source);
		void acknowledgeGateIrq();
		uint8_t panelEvent();
		void queuePanelEvent(uint32_t _switchIndex, bool _pressed);
		void armGateTimer(unsigned _timer);
		static void onGateTimer1(void* _self, uint64_t _when, uint64_t _now);
		static void onGateTimer2(void* _self, uint64_t _when, uint64_t _now);
		void gateTimerFired(unsigned _timer, uint64_t _when);
		void tickUsb();
		bool lcdDmaWanted() const;
		static void onLcdDma(void* _self, uint64_t _when, uint64_t _now);
		void lcdDmaTick(uint64_t _when);

		uint8_t programFlashRead(uint32_t _addr) const;
		void programFlashWrite(uint32_t _addr, uint8_t _value);
		void programFlashWriteWord(uint32_t _addr, uint16_t _value);
		void setFlashMode(FlashMode _mode);
		void invalidateProgramFlash(uint32_t _offset, uint32_t _size);
		void storeProgramByte(uint32_t _offset, uint8_t _value);
		uint32_t workRamRead32(uint32_t _address);
		void runFactoryReset();
		void powerCycle();

		sh2::Machine m_machine{sh2::ChipModel::SH7016, 2};
		FlashDevice m_flashDevice{*this};
		LcdDevice m_lcdDevice{*this};
		UsbDevice m_usbDevice{*this};
		LspDevice m_lspDevice{*this};
		GaDevice m_gaDevice{*this};
		std::array<xpLib::XP, 2> m_xp{};
		XpDevice m_xpDevice0{*this, m_xp[0]};
		XpDevice m_xpDevice1{*this, m_xp[1]};

		std::vector<uint8_t> m_cpuRom;
		std::vector<uint8_t> m_programRom;
		std::vector<uint8_t> m_dataRom;
		std::vector<uint8_t> m_waveRom;

		std::array<bool, 2> m_xpEnabled{};
		std::array<int32_t, 8> m_xp0SdiaInput{};
		std::array<int32_t, 2> m_xp0SdibInput{};
		std::array<int32_t, 8> m_xp1SdiaInput{};
		Lsp m_lsp;
		hwLib::SED1335 m_lcd{LcdWidth, LcdHeight, LcdVramSize};
		hwLib::Tc160g22af m_gateArray;
		bool m_gateIrqPending = false;
		bool m_ledPortWritten = false;
		uint16_t m_gateQueuedSources = 0;
		emu::Scheduler::EventId m_gateTimerEvent[2] = {0, 0};
		emu::Scheduler::EventId m_lcdDmaEvent = 0;

		bool m_valid = false;
		bool m_lspEnabled = true;
		uint64_t m_cycleTarget = 0;
		uint32_t m_panelButtons = 0;
		uint8_t m_panelEventCursor = 0;
		std::deque<uint8_t> m_panelEvents;
		std::deque<int8_t> m_encoderEvents;
		Sc8850SubMcu m_usb;
		std::array<std::vector<uint8_t>, 2> m_midiOut;
		MidiTransport m_midiTransport = MidiTransport::Usb;
		FlashMode m_programFlashMode = FlashMode::ReadArray;
		uint16_t m_programFlashStatus = 0x0080;
		uint32_t m_programFlashPendingAddress = 0xffffffff;
		uint8_t m_programFlashPendingHigh = 0xff;
	};
} // namespace emu88Lib
