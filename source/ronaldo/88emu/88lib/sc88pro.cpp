#include "sc88pro.h"

#include "rom.h"

#include <algorithm>
#include <cstdio>

namespace emu88Lib
{
	namespace
	{
		// States between an LCD register write and its ready pulse. One
		// instruction's worth: the real busy time is unmeasured, and the
		// firmware only needs the edge to arrive after its own write.
		constexpr uint64_t g_lcdReadyDelay = 12;
		constexpr uint8_t  g_gaLineLcd = 0;		// reports as status 1
		constexpr uint16_t g_analogBattery = 0x2a0;

		// Data-register offset within the port block (H'FE80) for ports 1-8,
		// so the port hooks keep addressing the board's handlers by the SFR
		// address the firmware map documents.
		constexpr uint8_t g_portDrOffset[9] = { 0, 0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f };
		constexpr uint32_t MidiBitsPerByte = 10;
		constexpr uint32_t MidiBaud = 31250;
		constexpr uint32_t g_powerOnMidiDelay = g_sampleRate;

		std::vector<uint8_t> decodeWaveRom(const std::vector<uint8_t>& _raw)
		{
			if(_raw.size() != Sc88Pro::WaveRomSize)
				return _raw;
			std::vector<uint8_t> decoded(_raw.size());
			WaveRom::unscramble(_raw.data(), _raw.size(), decoded.data(), decoded.size());
			return decoded;
		}
	}

	Sc88Pro::Sc88Pro(std::vector<uint8_t> _firmware, const std::vector<uint8_t>& _waveRom,
	                 const bool _factoryReset)
		: m_rom(std::move(_firmware))
		, m_waveRom(decodeWaveRom(_waveRom))
		, m_xp()
		, m_subMcu([this](Sc88SubMcu::Record&& _r) { m_midiInQueue.emplace_back(std::move(_r)); },
		           SmSysExStage, SmCommand - SmSysExStage)
	{
		if(m_rom.size() != RomSize)
		{
			std::fprintf(stderr, "Sc88Pro: firmware must be %u bytes, got %zu\n",
			             RomSize, m_rom.size());
			return;
		}

		// Configure the existing xp core from the board: the Pro exposes
		// five consecutive 4 MiB CS windows (A0/A1, B0/B1, C). Keep this
		// product wiring here; xp itself remains product-agnostic.
		constexpr size_t windowSize = 0x400000;
		for(size_t cs = 0; cs < 5; ++cs)
		{
			const size_t offset = cs * windowSize;
			if(offset >= m_waveRom.size())
				break;
			const size_t size = std::min(windowSize, m_waveRom.size() - offset);
			m_xp.mapWaveRom(cs, m_waveRom.data() + offset, size,
			                   xpLib::XP::PhysicalWaveRomWidth::bits16);
		}
		// SDOB is the stereo LSP send. With the Pro's 0x3924=0x5040 and
		// 0x3926=0x10d2 descriptors, SDOC and SDOD are the independent stereo
		// OUT1 and OUT2 streams.

		// Bus map + chip host hooks.
		wireChip();

		// The XP is on IRQ0 here. (The SC-88 puts the gate array there and the
		// XP on IRQ1; on the Pro the gate array shares IRQ0 through its own
		// status register, same as the SC-88's arrangement one line over.)
		m_xp.setInterruptCallback([this](const bool _level) { requestIrq(IrqXp, _level); });

		// IRQ2 vector == the unused-vector stub -> no sub-MCU on this board, so
		// MIDI arrives on the SCI. Vector 37 is IRQ2; vector 2 is a known stub.
		{
			const auto vec = [this](const int v)
			{
				const uint8_t* p = m_rom.data() + v * 4;
				return uint32_t(p[1] << 16) | uint32_t(p[2] << 8) | p[3];
			};
			m_serialMidi = vec(37) == vec(2);
		}

		m_valid = true;
		powerCycle();

		// A new emulator instance has blank battery SRAM. Initialise it through
		// the SC-88Pro firmware and panel, just as the physical owner's manual
		// prescribes. The headless VE-GS Pro ROM has no panel sub-MCU, so the
		// sequence applies only to the hardware SC-88Pro path.
		if(_factoryReset && !m_serialMidi)
			runFactoryReset();
	}

	void Sc88Pro::powerCycle()
	{
		// Battery SRAM and ROM mappings survive. Reset the CPU and every volatile
		// device/latch so the next sample is a genuine power-on boot.
		m_xp.reset();
		m_machine.reset();
		m_lcd.reset();
		m_lsp.clear();

		m_samplesRendered = 0;
		// The chip's state counter is monotonic across a reset, so the pacing
		// target rebases onto it rather than onto zero.
		m_cycleTarget = m_machine.now();
		m_cycleFrac = 0;

		m_buttons = 0;
		m_scanColumn = 0;
		m_gaInt.fill(false);
		m_gaIrqMask = 0x0f;
		m_gaIntTrigger = 0;
		m_leds = 0;
		m_lcdInstr = 0;
		m_lcdStaged = 0;
		m_lcdBuffer.fill(0);
		if(m_gaLcdEvent)
		{
			m_machine.sched().cancel(m_gaLcdEvent);
			m_gaLcdEvent = 0;
		}

		m_subMcuRam.fill(0);
		m_subMcuStarted = false;
		m_subMcu.reset();
		m_midiInQueue.clear();
		m_midiMailbox = {};
		m_midiMailboxFull = false;
		m_midiWireDelay = 0;
		m_midiWireFrac = 0;

		m_p3dr = 0;
		m_lspReturnEnabled = false;
		m_serialOut[0].clear();
		m_serialOut[1].clear();
	}

	// =====================================================================
	// Factory reset
	// =====================================================================

	void Sc88Pro::runFactoryReset()
	{
		if(!m_valid || m_serialMidi)
			return;

		// The owner's-manual procedure is:
		//
		//   hold SELECT and press both INSTRUMENT arrows -> "Init. All. Sure?"
		//   press ALL                                   -> execute
		//
		// Drive those real matrix inputs on a fixed sample timeline. This keeps
		// the factory SRAM image and every retained default owned by the firmware
		// instead of reproducing only a few visible parameters with MIDI DT1s.
		constexpr uint32_t kBootWait      = 8 * g_sampleRate;
		constexpr uint32_t kModifierLead  = g_sampleRate / 20;		// 50 ms
		constexpr uint32_t kChordHold     = g_sampleRate / 10;		// 100 ms
		constexpr uint32_t kKeyGap        = g_sampleRate / 4;		// 250 ms
		constexpr uint32_t kExecuteHold   = g_sampleRate / 10;		// 100 ms
		constexpr uint32_t kExecuteSettle = 2 * g_sampleRate;

		const auto run = [this](uint32_t _samples)
		{
			while(_samples-- > 0)
				renderSample();
		};
		const auto bit = [](const Sc88ProButton _button)
		{
			return uint32_t{1} << static_cast<uint8_t>(_button);
		};

		// Silent setup pass - see the same note in Sc88::runFactoryReset. XPINT
		// is measured silent here too, and the LSP goes with the XP: it is an
		// audio insert on SDOB with no path back to the H8.
		const auto xpEnabled = m_xpEnabled;
		const auto lspEnabled = m_lspEnabled;
		m_xpEnabled = false;
		m_lspEnabled = false;

		run(kBootWait);

		setButtons(bit(Sc88ProButton::Select));
		run(kModifierLead);
		setButtons(bit(Sc88ProButton::Select) |
		           bit(Sc88ProButton::InstL) | bit(Sc88ProButton::InstR));
		run(kChordHold);
		setButtons(bit(Sc88ProButton::Select));
		run(kModifierLead);
		setButtons(0);
		run(kKeyGap);

		setButtons(bit(Sc88ProButton::InstAll));
		run(kExecuteHold);
		setButtons(0);
		run(kExecuteSettle);

		// Factory Init resets Parts, System parameters and User data, but the
		// firmware preserves the currently selected compatibility map — and its
		// own factory image leaves SC-88 MAP selected, whereas the owner's
		// manual defines Native (both map LEDs dark) as the factory default. So
		// the map is cleared AFTER Init All, or the init would restore it. The
		// panel-map state persists in battery SRAM and gates the part-map init
		// at 01:4064 (@FE38 bits 2/3): with SC-88 MAP latched, every GS reset
		// puts the parts on the SC-88 tone set, and the whole SC-88Pro-era tone
		// map answers "No INSTRUMENT".
		//
		// The Pro has dedicated SC-55/SC-88 MAP buttons, so clearing a lit map
		// is a press of that same button, and repeating one button converges on
		// Native from either compatibility map. Do NOT use the SC-88's
		// SELECT+ALL chord here: on this panel it edits the system mode
		// parameter instead (its handler at 00:52BC writes SRAM 0xB07A), and a
		// nonzero mode byte disables the same tone set through the bit-5 flags
		// in the tone directory — Init All does not clear that byte either.
		constexpr uint8_t kMapLedMask = (1u << 2) | (1u << 3);
		for(int attempt = 0; attempt < 3 && (m_leds & kMapLedMask); ++attempt)
		{
			setButtons(bit(Sc88ProButton::Sc55Map));
			run(kChordHold);
			setButtons(0);
			run(kKeyGap);
		}

		m_xpEnabled = xpEnabled;
		m_lspEnabled = lspEnabled;

		// Boot again from the firmware-written battery SRAM. In particular, the
		// factory Native-map state is applied during this power-on path rather
		// than leaving the compatibility-map state from the setup pass alive.
		powerCycle();
	}

	// =====================================================================
	// Bus
	// =====================================================================

	uint8_t Sc88Pro::extRead8(const uint32_t _addr)
	{
		const uint8_t page = static_cast<uint8_t>(_addr >> 16);
		const uint16_t off = static_cast<uint16_t>(_addr);

		uint8_t v = 0xff;
		if(page <= PageRomLast)
			v = m_rom[_addr & (RomSize - 1)];		// mirrored every 1 MiB
		else if(page >= PageSramFirst && page <= PageSramLast)
			v = m_sram[off];
		else if(page == PageXp)
			v = m_xp.hostRead8(off);
		else if(page >= PageSubMcuFirst && page <= PageSubMcuLast)
			v = subMcuRead(off);
		else if(page == PageGateArray)
			v = gateArrayRead(off);
		else if(page == PageLsp)
			v = m_lsp.hostRead(off);

		return v;
	}

	void Sc88Pro::extWrite8(const uint32_t _addr, const uint8_t _val)
	{
		const uint8_t page = static_cast<uint8_t>(_addr >> 16);
		const uint16_t off = static_cast<uint16_t>(_addr);

		if(page >= PageSramFirst && page <= PageSramLast)
			m_sram[off] = _val;
		else if(page == PageXp)
			m_xp.hostWrite8(off, _val);
		else if(page >= PageSubMcuFirst && page <= PageSubMcuLast)
			subMcuWrite(off, _val);
		else if(page == PageGateArray)
			gateArrayWrite(off, _val);
		else if(page == PageLsp)
			m_lsp.hostWrite(off, _val);
		// ROM writes are dropped
	}

	uint8_t Sc88Pro::portRead(const uint32_t _addr, const uint8_t _value)
	{
		switch(_addr)
		{
		case 0xfe86: // P3DR; bit 7 gates the LSP return into XP SDIA5.
			return m_p3dr;
		case 0xfe87:	// P4DR — the reference model returns 0 here
			return 0x00;
		case 0xfe8a:	// P5DR — hardwired board-configuration straps.
			return P5Data;
		case 0xfe8e:
			// Sampled once at boot into 0xFE38 (00:8D99). Bits 2/3 are the
			// compatibility-map straps: either bit set makes every GS reset
			// rewrite all parts' tone map to SC-55/SC-88 (01:4064), which turns
			// the whole SC-88Pro-era tone set into "No INSTRUMENT". The real
			// board reads them low — Native map.
			return 0xf3;
		case 0xfe8f:
		{
			// Bit 1 is the XP interrupt line read back, active low; the XP
			// event drain loop terminates on it exactly as on the SC-88.
			const auto pending = m_xp.interruptState();
			return pending ? uint8_t(0xfd) : uint8_t(0xff);
		}
		default:
			// The Pro board's remaining unconnected port inputs are pulled high
			// — which is what the port model reports for a pin nothing drives.
			(void)_value;
			return 0xff;
		}
	}

	void Sc88Pro::portWrite(const uint32_t _addr, const uint8_t _val)
	{
		if(_addr == 0xfe86)
		{
			m_p3dr = _val;
			m_lspReturnEnabled = _val & 0x80;
		}
	}

	// =====================================================================
	// Sub-MCU (pages 0xE0-0xE7)
	// =====================================================================

	uint8_t Sc88Pro::subMcuRead(const uint16_t _addr)
	{
		switch(_addr)
		{
		case SmVersionHi:	return 0x01;
		case SmVersionLo:	return 0x23;

		case SmCommand:
			requestIrq(IrqSubMcu, false);
			m_midiMailboxFull = false;
			return m_midiMailbox.command;
		case SmChannel:		return m_midiMailbox.channel;
		case SmParam1:		return m_midiMailbox.param1;
		case SmParam2:		return m_midiMailbox.param2;

		case SmSemaphore:	return 0x80;

		case SmPanelData:
		{
			uint8_t rows = 0xff;
			for(uint8_t col = 0; col < 4; ++col)
			{
				if(m_scanColumn & (1u << col))
					rows &= static_cast<uint8_t>(~((m_buttons >> (col * 8)) & 0xff));
			}
			return rows;
		}

		default:
			return _addr < SmWindowSize ? m_subMcuRam[_addr] : uint8_t(0xff);
		}
	}

	void Sc88Pro::subMcuWrite(const uint16_t _addr, const uint8_t _val)
	{
		if(_addr >= SmWindowSize)
			return;
		m_subMcuRam[_addr] = _val;
		if(_addr == SmStart)
			m_subMcuStarted = true;
		else if(_addr == SmPanelData)
			m_scanColumn = _val & 0x0f;
	}

	// =====================================================================
	// Gate array (page 0xEF)
	// =====================================================================

	uint8_t Sc88Pro::gateArrayRead(const uint16_t _addr)
	{
		switch(_addr)
		{
		case GaLeds:
			return m_leds;
		case GaIrqMask:
			return m_gaIrqMask;
		case GaIrqStatus:
		{
			const uint8_t s = m_gaIntTrigger;
			m_gaIntTrigger = 0;
			requestIrq(IrqGateArray, false);
			return s;
		}
		default:
			return 0xff;
		}
	}

	void Sc88Pro::gateArrayWrite(const uint16_t _addr, const uint8_t _val)
	{
		switch(_addr)
		{
		case GaLeds:
			m_leds = _val;
			return;
		case GaIrqMask:
			m_gaIrqMask = _val;
			return;
		case GaLcdInstr:
			m_lcdInstr = _val;
			return;
		case GaLcdStart:
			if(!(_val & 1))
				m_lcd.write(false, m_lcdInstr);
			for(uint32_t i = 0; i < m_lcdStaged; ++i)
				m_lcd.write(true, m_lcdBuffer[i]);
			m_lcdStaged = 0;
			// The gate array pulses its LCD-ready line shortly after a burst.
			if(m_gaLcdEvent)
				m_machine.sched().cancel(m_gaLcdEvent);
			m_gaLcdEvent = m_machine.sched().schedule(m_machine.now() + g_lcdReadyDelay,
				[](void* _self, uint64_t, uint64_t)
				{
					auto* board = static_cast<Sc88Pro*>(_self);
					board->m_gaLcdEvent = 0;
					board->setGateArrayInt(g_gaLineLcd, false);
					board->setGateArrayInt(g_gaLineLcd, true);
				}, this);
			return;
		default:
			if(_addr >= GaLcdData && _addr <= GaLcdDataEnd)
			{
				const uint32_t idx = _addr - GaLcdData;
				m_lcdBuffer[idx] = _val;
				if(idx >= m_lcdStaged)
					m_lcdStaged = static_cast<uint8_t>(idx + 1);
			}
			return;
		}
	}

	void Sc88Pro::setGateArrayInt(const uint8_t _line, const bool _level)
	{
		if(_line >= m_gaInt.size())
			return;
		if(_level && !m_gaInt[_line] && !(m_gaIrqMask & (1u << _line)))
			m_gaIntTrigger = static_cast<uint8_t>(_line + 1);
		m_gaInt[_line] = _level;
		requestIrq(IrqGateArray, m_gaIntTrigger != 0);
	}

	// =====================================================================
	// MIDI
	// =====================================================================

	void Sc88Pro::uartTransmit (const uint8_t _byte) { m_serialOut[0].push_back(_byte); }
	void Sc88Pro::uart2Transmit(const uint8_t _byte) { m_serialOut[1].push_back(_byte); }

	void Sc88Pro::addMidiEvent(const synthLib::SMidiEvent& _event, const uint8_t _port)
	{
		if(m_serialMidi)
		{
			const auto push = [this, _port](const uint8_t _b)
			{
				if(_port & 1) uart2Receive(_b); else uartReceive(_b);
			};
			if(!_event.sysex.empty())
			{
				for(const auto b : _event.sysex)
					push(b);
				return;
			}
			if(!_event.a)
				return;
			push(_event.a);
			if(_event.a < 0xf0)
			{
				push(_event.b);
				const uint8_t st = _event.a & 0xf0;
				if(st != 0xc0 && st != 0xd0)
					push(_event.c);
			}
			return;
		}

		const uint8_t src = _port < Sc88SubMcu::SourceCount ? _port : 0;
		if(!_event.sysex.empty())
		{
			for(const auto b : _event.sysex)
				m_subMcu.midiIn(src, b);
			return;
		}
		if(!_event.a)
			return;
		m_subMcu.midiIn(src, _event.a);
		const uint8_t status = _event.a & 0xf0;
		const bool oneByte = status == 0xc0 || status == 0xd0;
		if(_event.a < 0xf0)
		{
			m_subMcu.midiIn(src, _event.b);
			if(!oneByte)
				m_subMcu.midiIn(src, _event.c);
		}
	}

	void Sc88Pro::pumpMidiIn()
	{
		if(!m_subMcuStarted || m_samplesRendered < g_powerOnMidiDelay)
			return;

		if(m_midiWireDelay)
		{
			--m_midiWireDelay;
			return;
		}
		if(m_midiMailboxFull || m_midiInQueue.empty())
			return;

		auto& r = m_midiInQueue.front();
		for(size_t i = 0; i < r.payload.size() && SmSysExStage + i < SmWindowSize; ++i)
			m_subMcuRam[SmSysExStage + i] = r.payload[i];

		m_midiMailbox = r;
		m_midiMailboxFull = true;

		// Wire pacing, as on the SC-88: without it the mailbox delivers a
		// message per audio frame and overruns the firmware's ring.
		const uint32_t samples = r.wireBytes * MidiBitsPerByte * g_sampleRate;
		m_midiWireDelay = samples / MidiBaud;
		m_midiWireFrac += samples % MidiBaud;
		if(m_midiWireFrac >= MidiBaud)
		{
			m_midiWireFrac -= MidiBaud;
			++m_midiWireDelay;
		}

		m_midiInQueue.pop_front();
		requestIrq(IrqSubMcu, true);
	}

	// =====================================================================
	// Frame
	// =====================================================================

	void Sc88Pro::wireChip()
	{
		auto& bus = m_machine.bus();

		// Mask ROM, 1 MiB. Pages 0x00-0x0F are the image itself and every page
		// the firmware fetches from, so they go into the bus image; page 0
		// stops at the register field, which the chip decodes on-chip. The
		// higher mirrors up to page 0x7F are data only and reach the board's
		// page decode.
		bus.map_rom(0x00000, 0xfe80, h8500::BusClass::W16_S2);
		bus.load(0x00000, m_rom.data(), 0xfe80);
		bus.map_rom(0x10000, RomSize - 0x10000, h8500::BusClass::W16_S2);
		bus.load(0x10000, m_rom.data() + 0x10000, RomSize - 0x10000);
		bus.map_device(0x100000, 0x800000 - 0x100000, &m_boardBus, h8500::BusClass::W16_S2);

		bus.map_device(0xc00000, 0x090000, &m_boardBus, h8500::BusClass::W16_S2);	// SRAM (C0-C7) + XP (C8)
		bus.map_device(0xe00000, 0x080000, &m_boardBus, h8500::BusClass::W16_S3);	// sub-MCU
		bus.map_device(0xef0000, 0x020000, &m_boardBus, h8500::BusClass::W16_S3);	// gate array + LSP
		m_machine.cpu().invalidate_all();

		// Port data registers: reads go through portRead so the board can
		// substitute the pins it drives, writes reach portWrite.
		m_machine.ports().set_read_hook([this](const unsigned _port, const uint8_t _value)
		{
			return portRead(0xfe80u + g_portDrOffset[_port], _value);
		});
		m_machine.ports().set_write_hook([this](const unsigned _port, const uint8_t _dr, uint8_t)
		{
			m_machine.ports().set_pins(_port, _dr);
			portWrite(0xfe80u + g_portDrOffset[_port], _dr);
		});

		// Only channel 0 is wired, to the battery sense.
		m_machine.adc().set_sampler([](const unsigned _channel) -> uint16_t
		{
			return _channel == 0 ? g_analogBattery : uint16_t(0);
		});

		m_machine.sci(0).set_tx_sink([this](const uint8_t _byte, uint64_t) { uartTransmit(_byte); });
		m_machine.sci(1).set_tx_sink([this](const uint8_t _byte, uint64_t) { uart2Transmit(_byte); });
	}

	Sc88Pro::SampleFrame Sc88Pro::renderSample()
	{
		if(!m_valid)
			return {0, 0};

		++m_samplesRendered;
		pumpMidiIn();

		// One audio frame of phi. This is the part's own clock: the core
		// charges every instruction its Appendix-A.4 states, so the CPU budget
		// and the peripheral clock are the same thing.
		m_cycleTarget += g_cpuClockHz / g_sampleRate;
		m_cycleFrac   += static_cast<uint32_t>(g_cpuClockHz % g_sampleRate);
		if(m_cycleFrac >= g_sampleRate)
		{
			m_cycleFrac -= g_sampleRate;
			++m_cycleTarget;
		}

		if(m_cycleTarget > cycles())
			m_machine.run(m_cycleTarget - cycles());

		if(!m_xpEnabled)
			return {0, 0};

		m_xp.step();
		auto& xpDsp = m_xp.dsp();
		const auto& busB = xpDsp.serialOutput(xpLib::Dsp::SerialBus::b);
		const auto& busC = xpDsp.serialOutput(xpLib::Dsp::SerialBus::c);
		// The board's main output is the stereo SDOC/OUT1 stream. SDOD is the
		// separate stereo OUT2 stream, not OUT1's right channel.
		const auto newOutput = xpDsp.serialOutputCount(xpLib::Dsp::SerialBus::c) >= 2
			? SampleFrame{xpLib::XP::serialWordToOutput(busC[0]), xpLib::XP::serialWordToOutput(busC[1])}
			: SampleFrame{0, 0};
		std::array<int32_t, 2> returns{};
		if(m_lspEnabled)
		{
			const auto sendCount = xpDsp.serialOutputCount(xpLib::Dsp::SerialBus::b);
			// SDOB carries one LSP word in each half-frame, and the first word
			// is the LEFT channel end to end: the LSP core names its first
			// program half "right" after the reference emulator, but on this
			// board a hard-left part has to reach OD1 of "59: OD1 / OD2" (the
			// manual routes L to OD1), and the 12DIST_G demo intro, which only
			// plays left-panned parts, is 8.7 dB left-heavy on the real unit.
			// Feeding word 0 as the core's left channel and returning the
			// core's left as word 0 reproduces that (8.1 dB); either half
			// swapped alone mirrors the effect, which is what made the two
			// overdrives trade places.
			const SampleFrame lspInput{
				sendCount > 0 ? busB[0] : 0,
				sendCount > 1 ? busB[1] : 0,
			};
			const auto lspOutput = m_lsp.process(lspInput);
			// The return enters the XP eight bits below the LSP's 24-bit datapath.
			// The production program maps SDIA to OUT1 with gain 255.46875, while
			// its mixer-to-SDOB send gain is 64. Dividing the return by 256 makes
			// the closed insert path near-unity (0.997925 of the send level).
			if(m_lspReturnEnabled)
				returns = {lspOutput.first >> 8, lspOutput.second >> 8};
		}
		xpDsp.setSerialInput(xpLib::Dsp::SerialBus::a, returns.data(), returns.size());
		return newOutput;
	}
}
