#include "sc88.h"
#include "baseLib/md5.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace emu88Lib
{
	namespace
	{
		// SmStart precedes the firmware's final part-state rebuild. Keep incoming
		// playback MIDI behind a conservative one-second power-on gate.
		constexpr uint32_t g_powerOnMidiDelay = g_sampleRate;

		// Analog input default. The firmware converts all four channels at boot
		// (01:6993) and again in the factory battery test; only channel 1 feeds
		// anything (00:0AF1 -> 08:FE2E).
		constexpr uint16_t g_analogBattery = 0x2a0;

		// P5DR (0xFE8A) — hardware configuration straps, read exactly once, by
		// 00:029A. The firmware inverts bit 0 and latches the result in SRAM at
		// 08:FE28 and 08:FE2A, and 08:FE2A is then one of the hottest words in
		// the machine (~1M reads per song). What it decides:
		//
		//   bit 0     00:5D3E  mixer send 3: set = scale the part's level through
		//                      the curve at 00:0400, clear = start from 0x7F
		//             01:1643  which effects DSP image is uploaded, 07:8B02 or
		//                      07:91CE
		//   bits 2-4  01:1623  index into the DSP config word table at 07:99CA
		//
		// 0x2D is the value the NukedSC55 reference feeds the SC-88, so the
		// latched word is 0x2C. Not measured on hardware — and since it selects
		// the effects configuration, it is worth measuring.
		constexpr uint8_t g_p5drStraps = 0x2d;

		// Gate-array interrupt line the LCD pulses when a burst has been sent.
		// It is line 0, which the status register reports as 1 -- the only line
		// this firmware ever enables (ga_installIrqHandler, called once).
		constexpr uint8_t g_gaLineLcd = 0;

		// Data-register offset within the port block (H'FE80) for ports 1-8,
		// so the port hooks can keep addressing the board's handlers by SFR
		// address the way the firmware map documents them.
		constexpr uint8_t g_portDrOffset[9] = { 0, 0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f };

		// States between an LCD register write and its ready pulse. One
		// instruction's worth: the real busy time is unmeasured, and the
		// firmware only needs the edge to arrive after its own write.
		constexpr uint64_t g_lcdReadyDelay = 12;
	}

	Sc88::Sc88(std::vector<uint8_t> _firmware, std::vector<uint8_t> _waveRom,
	           const Model _model, const bool _factoryReset)
		: m_rom(std::move(_firmware))
		, m_waveRom(std::move(_waveRom))
		, m_model(_model)
		, m_p5dr(g_p5drStraps)
	{
		if(m_rom.size() != RomSize)
		{
			std::fprintf(stderr, "Sc88: firmware must be %u bytes, got %zu\n", RomSize, m_rom.size());
			return;
		}
		constexpr size_t waveChipSize = 0x200000;
		const auto waveChips = std::min(m_waveRom.size() / waveChipSize, xpLib::XP::nWaveChipSelects);
		for(size_t chip = 0; chip < waveChips; ++chip)
			m_xp.mapWaveRom(chip, m_waveRom.data() + chip * waveChipSize, waveChipSize,
			                       xpLib::XP::PhysicalWaveRomWidth::bits16);

		const auto hash = baseLib::MD5(m_rom);
		if(m_model == Model::Sc88 && hash == baseLib::MD5("0ac771782ea58a53af590ebdf140d517"))
		{
			m_releaseEg = 0x405a;
			m_releaseFlags = 0x3bda;
			m_voiceAllocation = 0xf609;
		}
		else if(m_model == Model::Sc88VL && hash == baseLib::MD5("25e016e93c8a44ba3c35584462b56d72"))
		{
			m_releaseEg = 0x40d6;
			m_releaseFlags = 0x3c56;
			m_voiceAllocation = 0xf61d;
		}

		// Bus map + chip host hooks.
		wireChip();

		// External interrupts: IRQ0 = gate array, IRQ1 = XPINT, IRQ2 = sub-MCU.
		m_xp.setInterruptCallback([this](const bool _level)
		{
			requestIrq(IrqXp, _level);
		});

		m_analog[0] = g_analogBattery;

		m_valid = true;
		powerCycle();
		if(_factoryReset)
			runFactoryReset();
	}

	void Sc88::powerCycle()
	{
		// Preserve only battery-backed SRAM and immutable board wiring. Everything
		// else below is volatile state that a physical power cycle clears.
		m_xp.reset();
		m_machine.reset();
		m_lcd.reset();
		m_lcdEnabled = true;

		m_samplesRendered = 0;
		m_pendingVoiceResets = 0;
		// The chip's state counter is monotonic across a reset, so the pacing
		// target rebases onto it rather than onto zero.
		m_cycleTarget = m_machine.now();
		m_cycleFrac = 0;

		m_buttons = 0;
		m_scanColumn = 0;
		m_panelCtrl = 0;
		m_leds = 0;
		m_gaInt.fill(false);
		m_gaIrqMask = 0x0f;
		m_gaIntTrigger = 0;
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

		m_midiOut.clear();
	}

	// =====================================================================
	// Factory reset
	// =====================================================================

	void Sc88::runFactoryReset()
	{
		if(!m_valid)
			return;

		// The SC-88 uses the same panel procedure as the SC-88Pro:
		//
		//   hold SELECT and press both INSTRUMENT arrows -> initialize prompt
		//   press ALL                                  -> execute
		//
		// Drive the real firmware so the complete battery-backed factory image,
		// rather than only the GS part state, is established from its ROM data.
		constexpr uint32_t kBootWait      = 8 * g_sampleRate;
		constexpr uint32_t kModifierLead  = g_sampleRate / 5;		// 200 ms
		constexpr uint32_t kChordHold     = g_sampleRate / 2;		// 500 ms
		constexpr uint32_t kKeyGap        = g_sampleRate / 4;		// 250 ms
		constexpr uint32_t kExecuteHold   = g_sampleRate / 10;		// 100 ms
		constexpr uint32_t kExecuteSettle = 2 * g_sampleRate;

		const auto run = [this](uint32_t _samples)
		{
			while(_samples-- > 0)
				renderSample();
		};

		// Nobody listens to a setup pass, so the XP is not clocked at all for
		// its duration. That is safe precisely here: the firmware starts no
		// voices, so XPINT (IRQ1) stays silent from the first sample to the
		// last - measured as zero edges - and the SRAM image this writes is
		// byte-identical with the chip idle. Anywhere a voice can sound, XPINT
		// is what retires them and the XP has to keep stepping.
		const auto xpEnabled = m_xpEnabled;
		m_xpEnabled = false;

		run(kBootWait);
		setButtons(buttonBit(Button::Select));
		run(kModifierLead);
		setButtons(buttonBit(Button::Select) |
		           buttonBit(Button::InstL) | buttonBit(Button::InstR));
		run(kChordHold);
		setButtons(buttonBit(Button::Select));
		run(kModifierLead);
		setButtons(0);
		run(kKeyGap);

		setButtons(buttonBit(Button::InstAll));
		run(kExecuteHold);
		setButtons(0);
		run(kExecuteSettle);

		m_xpEnabled = xpEnabled;

		// The manual describes initialization as restoring retained settings. A
		// real unit subsequently boots from them, so complete that power cycle
		// instead of returning with the pre-reset volatile part/map state alive.
		powerCycle();
	}

	// =====================================================================
	// Bus
	// =====================================================================

	uint8_t Sc88::extRead8(const uint32_t _addr)
	{
		const uint32_t page = _addr >> 16;
		const uint16_t off  = static_cast<uint16_t>(_addr);

		switch(page)
		{
		case 0x0:
			// The register field never reaches us — the chip decodes it on
			// chip — so everything from 0x8000 up is external SRAM.
			return off < 0x8000 ? m_rom[off] : m_sram[off];

		case 0x1: case 0x2: case 0x3:
		case 0x4: case 0x5: case 0x6: case 0x7:
			return m_rom[_addr & (RomSize - 1)];

		case 0x8:
			return m_sram[off];

		case 0xe:
			if(off >= 0x4000)
				return m_sram[off];
			return m_xp.hostRead8(off);

		case 0xf:
			return gateArrayRead(off);

		default:
			return 0xff;
		}
	}

	void Sc88::extWrite8(const uint32_t _addr, const uint8_t _val)
	{
		const uint32_t page = _addr >> 16;
		const uint16_t off  = static_cast<uint16_t>(_addr);

		switch(page)
		{
		case 0x0:
			if(off >= 0x8000)
			{
				m_sram[off] = _val;
				observeVoiceWrite(off);
			}
			// below 0x8000 is mask ROM — writes are dropped
			return;

		case 0x8:
			m_sram[off] = _val;
			observeVoiceWrite(off);
			return;

		case 0xe:
			if(off >= 0x4000)
			{
				m_sram[off] = _val;
				observeVoiceWrite(off);
				return;
			}
			m_xp.hostWrite8(off, _val);
			if(off >= 0x3900 && off < 0x3908 && (off & 1))
			{
				const auto first = ((off - 0x3900) / 2) * 16;
				for(unsigned voice = first; voice < first + 16; ++voice)
					if(!m_xp.state().voices[voice].resetState_3900.shadow)
						m_pendingVoiceResets &= ~(uint64_t{1} << voice);
			}
			return;

		case 0xf:
			gateArrayWrite(off, _val);
			return;

		default:
			return;
		}
	}

	// =====================================================================
	// I/O ports (0xFE80-0xFE8F)
	// =====================================================================

	uint8_t Sc88::portRead(const uint32_t _addr, const uint8_t _value)
	{
		switch(_addr)
		{
		case 0xfe86:	// P3DR
			return 0x00;
		case 0xfe87:	// P4DR
			return 0x00;
		case 0xfe8a:	// P5DR — the board's configuration straps. See g_p5drStraps.
			return m_p5dr;

		case 0xfe8f:
		{
			// Bit 1 is the XP's interrupt line brought back as a port bit,
			// ACTIVE LOW: clear = an event is queued, set = nothing pending.
			//
			// This is how the XPINT handler terminates. Its drain loop is
			//     00:539c  MOV:G.W @0x391a, R0    ; read data (acknowledges)
			//     00:53a0  BTST.B #1, @0x8f       ; DP = 0xFE, so port 0xFE8F
			//     00:53a3  BEQ.W  0x52d1          ; still asserted -> read again
			// i.e. it keeps servicing events until this bit goes high. It does
			// NOT decide from the status register, so returning a "queue empty"
			// status is not enough — without this bit the handler spins forever
			// and the firmware locks up.
			const auto pending = m_xp.interruptState();
			return pending ? static_cast<uint8_t>(_value & ~0x02)
			               : static_cast<uint8_t>(_value | 0x02);
		}
		default:
			// Ports with nothing wired to them read back what was last written
			// — portWrite mirrors every latch onto the pins for exactly that.
			return _value;
		}
	}

	void Sc88::portWrite(const uint32_t _addr, const uint8_t _val)
	{
		// P6DR bit 0 is the LCD power/enable line on the SC-88VL; on the plain
		// SC-88 it is something else (the firmware drives it during normal
		// operation, so reading it as an enable blanks the display). Hence the
		// model gate — the reference emulator makes the same distinction.
		if(_addr == 0xfe8b && m_model == Model::Sc88VL)
			m_lcdEnabled = (_val & 1) != 0;
		// 0xFE87 (P4DR) is the LCD contrast DAC, cosmetic.
	}

	// MIDI OUT is routed through the sub-MCU; the H8 UART outputs are disconnected.
	void Sc88::uartTransmit(const uint8_t _byte)
	{
		(void)_byte;
	}

	void Sc88::uart2Transmit(const uint8_t _byte)
	{
		(void)_byte;
	}

	// =====================================================================
	// Sub-MCU (page 0x0F, 0x0000-0x00FF)
	// =====================================================================
	//
	// A peripheral controller that owns both MIDI DINs and the front-panel
	// matrix, modeled through its mailbox protocol.

	uint8_t Sc88::subMcuRead(const uint16_t _addr)
	{
		switch(_addr)
		{
		case SmVersionHi:	return 0x01;
		case SmVersionLo:	return 0x23;

		case SmCommand:
			// Reading the opcode acknowledges the message: drop IRQ2 and free
			// the mailbox for the next one.
			requestIrq(IrqSubMcu, false);
			m_midiMailboxFull = false;
			return m_midiMailbox.command;
		case SmChannel:		return m_midiMailbox.channel;
		case SmParam1:		return m_midiMailbox.param1;
		case SmParam2:		return m_midiMailbox.param2;

		case SmSemaphore:	return 0x80;	// always ready

		case SmPanelData:
		{
			// Only bits 0-3 are the one-hot column select; the firmware ORs
			// other output bits into the same write (panel_scanMatrix reads
			// them from SRAM 08:5071). Rows read back active low.
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

	void Sc88::subMcuWrite(const uint16_t _addr, const uint8_t _val)
	{
		if(_addr >= SmWindowSize)
			return;

		m_subMcuRam[_addr] = _val;

		switch(_addr)
		{
		case SmStart:
			m_subMcuStarted = true;
			break;
		case SmPanelData:
			m_scanColumn = _val & 0x0f;
			break;
		case SmPanelCtrl:
			m_panelCtrl = _val;
			break;
		default:
			break;
		}
	}

	// =====================================================================
	// Gate array (page 0x0F, 0xC100-0xC12C)
	// =====================================================================

	uint8_t Sc88::gateArrayRead(const uint16_t _addr)
	{

		if(_addr < SmWindowSize)
			return subMcuRead(_addr);

		switch(_addr)
		{
		case GaLeds:
			return m_leds;

		case GaIrqMask:
			return m_gaIrqMask;

		case GaIrqStatus:
		{
			const uint8_t status = m_gaIntTrigger;
			m_gaIntTrigger = 0;
			requestIrq(IrqGateArray, false);
			return status;
		}

		default:
			return 0xff;
		}
	}

	void Sc88::gateArrayWrite(const uint16_t _addr, const uint8_t _val)
	{

		if(_addr < SmWindowSize)
		{
			subMcuWrite(_addr, _val);
			return;
		}

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
			lcdSendBurst((_val & 1) != 0);
			return;

		default:
			if(_addr >= GaLcdData && _addr <= GaLcdDataEnd)
			{
				const uint32_t idx = _addr - GaLcdData;
				m_lcdBuffer[idx] = _val;
				if(idx >= m_lcdStaged)
					m_lcdStaged = static_cast<uint8_t>(idx + 1);
			}
			// GaConfig1/2/3/7 are written once at init and never read back.
			return;
		}
	}

	// Send what the firmware staged: the instruction byte unless _dataOnly,
	// then every character written since the previous burst.
	void Sc88::lcdSendBurst(const bool _dataOnly)
	{
		if(!_dataOnly)
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
				auto* board = static_cast<Sc88*>(_self);
				board->m_gaLcdEvent = 0;
				board->setGateArrayInt(g_gaLineLcd, false);
				board->setGateArrayInt(g_gaLineLcd, true);
			}, this);
	}

	// Line n reports as status n+1 (ga_isr indexes its RAM vector table with
	// that value), while the mask bit for line n is bit n and 1 means masked.
	void Sc88::setGateArrayInt(const uint8_t _line, const bool _level)
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

	void Sc88::addMidiEvent(const synthLib::SMidiEvent& _event, const uint8_t _port)
	{
		const uint8_t source = _port & 3;

		// Editor transport (front-panel input, state requests) — consumed here,
		// never forwarded to the firmware.
		if(!_event.sysex.empty())
		{
			addSysEx(_event.sysex, _port);
			return;
		}

		// The host hands us parsed events; the real board receives a byte
		// stream. Serialise back to wire bytes and run them through the
		// sub-MCU's parser, so running status, realtime interleave and the
		// per-source stream state all behave as the hardware's MIDI controller
		// does, whatever the host representation was.
		const uint8_t status = _event.a & 0xf0;
		if(status < 0x80)
			return;

		m_subMcu.midiIn(source, _event.a);
		if(_event.a < 0xf8 && status != 0xf0)
		{
			m_subMcu.midiIn(source, _event.b & 0x7f);
			// Program change and channel pressure are two bytes on the wire,
			// everything else three.
			if(status != 0xc0 && status != 0xd0)
				m_subMcu.midiIn(source, _event.c & 0x7f);
		}
	}

	void Sc88::postRawMidiMessage(const uint8_t _command, const uint8_t _channel,
	                              const uint8_t _p1, const uint8_t _p2)
	{
		MidiMessage m;
		m.command = _command;
		m.channel = _channel;
		m.param1  = _p1;
		m.param2  = _p2;
		m_midiInQueue.push_back(m);
	}

	// Feed a complete SysEx through the sub-MCU's wire parser. The
	// classification, checksum validation, chunk splitting and record layout
	// all live in Sc88SubMcu — this is just the byte stream the wire would carry.
	void Sc88::addSysEx(const synthLib::SysexBuffer& _sysex, const uint8_t _port)
	{
		const uint8_t source = _port & 3;
		for(const auto b : _sysex)
			m_subMcu.midiIn(source, b);
	}

	void Sc88::pumpMidiIn()
	{
		if(!m_subMcuStarted || m_samplesRendered < g_powerOnMidiDelay)
			return;

		// Hold the message back until it would physically have arrived. See the
		// MidiBaud note in sc88.h: delivering at emulator speed instead of wire
		// speed overruns the firmware's ring buffer, and it then stops
		// allocating voices — the whole song goes quiet after ~30 s.
		if(m_midiWireDelay)
		{
			--m_midiWireDelay;
			return;
		}

		if(m_midiMailboxFull || m_midiInQueue.empty())
			return;

		m_midiMailbox = std::move(m_midiInQueue.front());
		m_midiInQueue.pop_front();

		// Stage this chunk's bytes now. The previous chunk has already been
		// copied out by the ISR — the wire delay below is what guarantees it,
		// since even a full 0x7f-byte chunk buys ~40 ms at 31250 baud.
		for(size_t i = 0; i < m_midiMailbox.payload.size(); ++i)
		{
			const size_t at = m_midiMailbox.param2 + i;
			if(at < SmWindowSize)
				m_subMcuRam[at] = m_midiMailbox.payload[i];
		}

		m_midiMailboxFull = true;
		requestIrq(IrqSubMcu, true);

		// samples = bytes * sampleRate * bitsPerByte / baud, carried in
		// fixed point so the rate is exact rather than rounded per message.
		const uint32_t units = uint32_t(m_midiMailbox.wireBytes) * g_sampleRate * MidiBitsPerByte + m_midiWireFrac;
		m_midiWireDelay = units / MidiBaud;
		m_midiWireFrac  = units % MidiBaud;
	}

	void Sc88::readMidiOut(std::vector<synthLib::SMidiEvent>& _events)
	{

		if(m_midiOut.empty())
			return;
		_events.insert(_events.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}

	// =====================================================================
	// Front panel
	// =====================================================================

	void Sc88::setButton(const Button _button, const bool _pressed)
	{
		setButton(static_cast<uint32_t>(_button), _pressed);
	}

	void Sc88::setButton(const uint32_t _index, const bool _pressed)
	{
		if(_index >= g_buttonCount)
			return;

		if(_pressed)
			m_buttons |= 1u << _index;
		else
			m_buttons &= ~(1u << _index);
	}

	// =====================================================================
	// Render
	// =====================================================================
	Sc88::SampleFrame Sc88::renderXpAudioFrame() const
	{
		const auto& dsp = m_xp.dsp();
		const auto& bus = dsp.serialOutput(xpLib::Dsp::SerialBus::c);
		return dsp.serialOutputCount(xpLib::Dsp::SerialBus::c) >= 2
			? SampleFrame{xpLib::XP::serialWordToOutput(bus[0]), xpLib::XP::serialWordToOutput(bus[1])}
			: SampleFrame{0, 0};
	}

	void Sc88::wireChip()
	{
		auto& bus = m_machine.bus();

		// Mask ROM: page 0 up to the SRAM overlay, then pages 1-7. Mapped into
		// the bus image so instruction fetch and the decode cache read it
		// directly instead of going through the board every byte.
		bus.map_rom(0x00000, 0x8000, h8500::BusClass::W16_S2);
		bus.load(0x00000, m_rom.data(), 0x8000);
		bus.map_rom(0x10000, RomSize - 0x10000, h8500::BusClass::W16_S2);
		bus.load(0x10000, m_rom.data() + 0x10000, RomSize - 0x10000);

		// Everything else reaches the board's page decode.
		bus.map_device(0x08000, 0xfe80 - 0x8000, &m_page0, h8500::BusClass::W16_S2);
		bus.map_device(0x80000, 0x10000, &m_page8, h8500::BusClass::W16_S2);
		bus.map_device(0xe0000, 0x10000, &m_pageE, h8500::BusClass::W16_S3);
		bus.map_device(0xf0000, 0x10000, &m_pageF, h8500::BusClass::W16_S3);
		m_machine.io().set_fallback(&m_regFieldTail);
		m_machine.cpu().invalidate_all();

		// Port data registers. Reads go through portRead so the board can
		// substitute the pins it drives; writes mirror the latch onto the pins
		// so an unwired port reads back what was last written.
		m_machine.ports().set_read_hook([this](const unsigned _port, const uint8_t _value)
		{
			return portRead(0xfe80u + g_portDrOffset[_port], _value);
		});
		m_machine.ports().set_write_hook([this](const unsigned _port, const uint8_t _dr, uint8_t)
		{
			m_machine.ports().set_pins(_port, _dr);
			portWrite(0xfe80u + g_portDrOffset[_port], _dr);
		});

		// The four analog inputs, as the host set them.
		m_machine.adc().set_sampler([this](const unsigned _channel) -> uint16_t
		{
			return _channel < m_analog.size() ? m_analog[_channel] : uint16_t(0);
		});

		m_machine.sci(0).set_tx_sink([this](const uint8_t _byte, uint64_t) { uartTransmit(_byte); });
		m_machine.sci(1).set_tx_sink([this](const uint8_t _byte, uint64_t) { uart2Transmit(_byte); });
	}

	void Sc88::observeVoiceWrite(const uint16_t _offset)
	{
		if(!m_releaseEg)
			return;
		if(_offset >= m_releaseEg && _offset < m_releaseEg + 128)
		{
			const auto voice = (_offset - m_releaseEg) / 2;
			const auto bit = uint64_t{1} << voice;
			const auto eg = (m_sram[m_releaseEg + voice * 2] << 8) | m_sram[m_releaseEg + voice * 2 + 1];
			if(eg)
				m_pendingVoiceResets &= ~bit;
			else if((_offset & 1) && (m_sram[m_releaseFlags + voice * 2] & 0x80))
				m_pendingVoiceResets |= bit;
		}
		if(_offset >= m_voiceAllocation && _offset < m_voiceAllocation + 128 && (_offset & 1))
		{
			const auto bit = uint64_t{1} << ((_offset - m_voiceAllocation) / 2);
			if(m_sram[_offset] == 0xff)
				m_pendingVoiceResets |= bit;
			else
				m_pendingVoiceResets &= ~bit;
		}
	}

	Sc88::SampleFrame Sc88::renderSample()
	{
		if(!m_valid)
			return {0, 0};

		++m_samplesRendered;

		pumpMidiIn();

		// Advance the H8 by one audio frame of phi. Integer Bresenham keeps
		// the ratio exact regardless of sample rate. This is the part's own
		// clock: the core charges every instruction its Appendix-A.4 states,
		// so the CPU budget and the peripheral clock are the same thing.
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

		if((m_samplesRendered & 127u) == 0 && m_pendingVoiceResets)
		{
			for(unsigned voice = 0; voice < 64; ++voice)
				if(m_pendingVoiceResets & (uint64_t{1} << voice))
					m_xp.retireVoice(voice);
			m_pendingVoiceResets = 0;
		}
		m_xp.step();
		return renderXpAudioFrame();
	}
}
