#include "88lib/boards/proe.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace emu88Lib
{
	namespace
	{
		// The power-on store of the system parameters 2451-2452 (STD at C083), and the protection bit.
		constexpr uint16_t g_powerOnDefaultsPc = 0xc083;
		constexpr uint32_t g_protectAddress = 0x2452;
		constexpr uint8_t g_protectBit = 0x80;

		// The bank latch's card select, and its choice of a 64 KiB card's half.
		constexpr uint8_t g_latchCard = 0x08;
		constexpr uint8_t g_latchCardHalf = 0x04;
		// BFFF of the card, bits 1-0: the slot's status, see the class comment.
		constexpr uint32_t g_cardStatusOffset = 0x7fff;
		constexpr uint8_t g_cardStatusMask = 0x03, g_cardPresent = 0x03;
		// selectRomBank()'s code banks past the pattern ROMs' 0-7: the card's halves, an empty slot,
		// and the ROM banks 8-15 the latch can name, which no chip answers.
		constexpr uint8_t g_codeBankCard = 8, g_codeBankNoCard = 10, g_codeBankUnmapped = 11;

		bool isE20Card(const std::vector<uint8_t>& _card, const size_t _base)
		{
			return _card.size() >= _base + 12 && std::memcmp(_card.data() + _base, "Roland E-20 ", 12) == 0;
		}

		// D4A3-D541: the 12 bytes from 4000 against "Roland PROE " or "Roland E-20 ", then the
		// format letter (D or N) and its complement, and X or Q (by format) and its complement.
		bool hasCardHeader(const std::vector<uint8_t>& _card, const size_t _base = 0)
		{
			if(_card.size() < _base + 16)
				return false;
			const auto letter = [&](const size_t _offset, const uint8_t _value)
			{
				return _card[_base + _offset] == _value &&
					_card[_base + _offset + 1] == static_cast<uint8_t>(~_value);
			};
			if(isE20Card(_card, _base))
				return letter(12, 'N') && letter(14, 'Q');
			if(std::memcmp(_card.data() + _base, "Roland PROE ", 12) == 0)
				return (letter(12, 'D') || letter(12, 'N')) && letter(14, 'X');
			return false;
		}

		// Printable text at _offset, up to _size bytes, a NUL or erased memory (FF), trailing spaces dropped.
		std::string cardText(const std::vector<uint8_t>& _card, const size_t _offset, const size_t _size)
		{
			std::string text;
			for(size_t i = _offset; i < _offset + _size && i < _card.size() && _card[i] && _card[i] != 0xff; ++i)
				text += _card[i] >= 0x20 && _card[i] < 0x7f ? static_cast<char>(_card[i]) : '?';
			while(!text.empty() && text.back() == ' ')
				text.pop_back();
			return text;
		}

		// The dial's PA7 pulses: low for 0.5 ms, high for 0.5 ms, two per detent.
		constexpr uint64_t g_dialLowStates = ProE::EClock / 2000;
		constexpr uint64_t g_dialHighStates = ProE::EClock / 2000;
		constexpr int g_dialPulsesPerDetent = 2;

		// Where each ProEButton sits in the key matrix: row (PA5-PA3), bit.
		struct MatrixPosition { uint8_t row, bit; };
		constexpr std::array<MatrixPosition, static_cast<size_t>(ProEButton::Count)> g_buttonPositions{{
			{5, 6},	// START/STOP
			{3, 4},	// INTRO/END
			{3, 1},	// FILL ORIGINAL
			{3, 3},	// FILL VARIATION
			{1, 7},	// SYNC START
			{1, 6},	// SYNC STOP
			{1, 4},	// ARR HOLD
			{1, 3},	// ADV ARR
			{1, 2},	// VARIATION
			{3, 5},	// TAP TEMPO / BREAK
			{5, 5},	// FADE IN
			{5, 4},	// FADE OUT
			{0, 0},	// STYLE
			{4, 1},	// GROUP A/B
			{7, 0}, {7, 1}, {7, 2}, {7, 3}, {7, 4}, {7, 5}, {7, 6}, {7, 7},	// BANK 1-8
			{1, 8}, {1, 9}, {1, 10}, {0, 8}, {0, 9}, {0, 10}, {7, 8}, {7, 9},	// NUMBER 1-8
			{1, 5},	// REV ON/OFF
			{7, 10},	// REV TYPE
			{0, 4},	// MIDI FNCT
			{0, 5},	// PARAMETER
			{6, 10},	// CURSOR
		}};
	}

	uint32_t ProE::leds() const
	{
		// Each ProELed's LED row and bit.
		static constexpr MatrixPosition positions[] = {
			{6, 6}, {4, 4}, {2, 7}, {2, 6}, {2, 4}, {2, 3}, {2, 2}, {6, 5}, {6, 4}, {1, 0}, {5, 1},
			{6, 3}, {6, 2}, {6, 1}, {6, 0},
			{2, 5}, {1, 4}, {1, 5},
		};
		static_assert(std::size(positions) == static_cast<size_t>(ProELed::Count));
		uint32_t result = 0;
		for(size_t i = 0; i < std::size(positions); ++i)
			if((m_leds[positions[i].row] >> positions[i].bit) & 1)
				result |= uint32_t{1} << i;
		return result;
	}

	bool ProE::isCardImage(const std::vector<uint8_t>& _image)
	{
		return (_image.size() == CardBankSize || _image.size() == MaxCardSize) && hasCardHeader(_image);
	}

	size_t ProE::cardCount(const std::vector<uint8_t>& _image)
	{
		if(isCardImage(_image))
			return 1;
		if(_image.size() <= MaxCardSize || _image.size() % CardBankSize)
			return 0;
		for(size_t base = 0; base < _image.size(); base += CardBankSize)
			if(!hasCardHeader(_image, base))
				return 0;
		return _image.size() / CardBankSize;
	}

	std::vector<uint8_t> ProE::cardInImage(const std::vector<uint8_t>& _image, const size_t _index)
	{
		const auto count = cardCount(_image);
		if(_index >= count)
			return {};
		if(count == 1)
			return _image;
		const auto begin = _image.begin() + static_cast<std::ptrdiff_t>(_index * CardBankSize);
		return {begin, begin + static_cast<std::ptrdiff_t>(CardBankSize)};
	}

	std::string ProE::cardLabel(const std::vector<uint8_t>& _image, const size_t _index)
	{
		const auto card = cardInImage(_image, _index);
		if(card.empty())
			return {};
		// The E-20 format: the ID at 10, the four style names of 16 characters from 30.
		auto label = cardText(card, 0x10, 16);
		if(!isE20Card(card, 0))
			return label;
		std::string styles;
		for(size_t i = 0; i < 4; ++i)
		{
			const auto name = cardText(card, 0x30 + i * 16, 16);
			if(!name.empty())
				styles += (styles.empty() ? "" : ", ") + name;
		}
		if(!styles.empty())
			label += (label.empty() ? "" : ": ") + styles;
		return label;
	}

	ProE::ProE(const ProERomSet& _roms, const LaRomSet& _soundBoard, std::vector<uint8_t> _card)
		: m_sound(_soundBoard)
	{
		if(!_roms.isValid())
			return;
		if(isCardImage(_card))
			m_card = std::move(_card);
		m_program = _roms.program;
		for(const auto& rom : _roms.patterns)
			m_patterns.insert(m_patterns.end(), rom.begin(), rom.end());

		auto& p = m_machine.periph();
		m_machine.map_device(IoBase, IoEnd - IoBase, &m_io);
		m_machine.map_device(RamWindow, RamWindowSize, &m_ramDevice);
		m_machine.map_rom(RomWindow, RomWindowSize);
		m_machine.map_rom(ProgramBase, static_cast<uint32_t>(m_program.size()));
		m_machine.bus().load(ProgramBase, m_program.data(), m_program.size());

		// The ACIA runs from the 2 MHz clock, which is E.
		m_acia.set_clock_period(1u << 16);
		m_acia.set_irq_hook([this](const bool _on) { m_machine.periph().set_xirq(_on); });
		m_acia.set_tx_hook([this](const uint8_t _byte) { m_midiOut.write(_byte); });
		p.set_tx_hook([this](const uint8_t _byte)
		{
			m_link.write(_byte);
			++m_linkBytes;
			if(m_linkMonitor)
				m_linkMonitor(_byte);
		});

		// PA5-PA3 select the key / LED row, PA2-PA0 return three more keys of it, PA7 is the dial's pulse.
		p.set_port_write_hook(m6801::Hc11Peripherals::kPortA, [this](const uint8_t _data, uint8_t)
		{
			ledIntegrate();
			m_keyRow = (_data >> 3) & 7;
		});
		p.set_port_read_hook(m6801::Hc11Peripherals::kPortA, [this] { return portAPins(); });
		// AN0-AN3, sampled once at power-on.
		for(unsigned i = 0; i < 4; ++i)
			p.set_analog_input(i, 0x80);

		m_valid = true;
		reset();
	}

	void ProE::reset()
	{
		if(!m_valid)
			return;
		m_acia.power_on();
		m_lcd.reset();
		m_lcdLines = m_lcdBus = m_lcdRead = 0;
		m_romBank = 0xff;
		m_ramBank = 0;
		selectRomBank(0);
		// A turn in progress does not carry over into the new boot, whose pulse divider starts afresh.
		if(m_dialEvent)
			m_machine.sched().cancel(m_dialEvent);
		m_dialEvent = 0;
		m_dialPending = m_dialPulses = 0;
		m_dialPulseLow = false;
		m_pa7 = true;
		m_machine.periph().set_port_input(m6801::Hc11Peripherals::kPortA, portAPins());
		m_machine.reset();
		m_sound.reset();
	}

	// ---------------------------------------------------------------------------
	// Audio and MIDI

	ProE::SampleFrame ProE::renderSample()
	{
		if(!isValid())
			return {};
		// 62.5 E cycles per sample of the CM-32L's 32 kHz.
		constexpr uint32_t whole = EClock / SampleRate;
		constexpr uint32_t remainder = EClock % SampleRate;
		m_cycleFraction += remainder;
		uint32_t cycles = whole;
		if(m_cycleFraction >= SampleRate)
		{
			m_cycleFraction -= SampleRate;
			++cycles;
		}
		m_machine.run(cycles);

		m_linkEvents.clear();
		m_link.getEvents(m_linkEvents);
		for(const auto& event : m_linkEvents)
			m_sound.addMidiEvent(event);
		return m_sound.renderSample();
	}

	void ProE::addMidiEvent(const synthLib::SMidiEvent& _event)
	{
		if(!m_valid)
			return;
		if(!_event.sysex.empty())
		{
			m_acia.receive(_event.sysex.data(), _event.sysex.size());
			return;
		}
		const uint8_t bytes[3] = {_event.a, _event.b, _event.c};
		const auto length = synthLib::MidiBufferParser::lengthFromStatusByte(_event.a);
		m_acia.receive(bytes, std::clamp<size_t>(length, 1, 3));
	}

	void ProE::readMidiOut(std::vector<synthLib::SMidiEvent>& _events)
	{
		m_midiOut.getEvents(_events);
	}

	void ProE::transportDiscontinuity(const uint32_t _generation)
	{
		m_sound.transportDiscontinuity(_generation);
	}

	// ---------------------------------------------------------------------------
	// Panel

	void ProE::setButtons(const uint64_t _buttons)
	{
		const auto changed = _buttons ^ m_buttons;
		m_buttons = _buttons;
		for(size_t i = 0; i < g_buttonPositions.size(); ++i)
		{
			if(changed & (uint64_t{1} << i))
				setKey(g_buttonPositions[i].row, g_buttonPositions[i].bit, (_buttons >> i) & 1);
		}
	}

	void ProE::setKey(const unsigned _row, const unsigned _bit, const bool _pressed)
	{
		if(_row >= m_keys.size() || _bit >= 11)
			return;
		const auto mask = static_cast<uint16_t>(1u << _bit);
		if(_pressed) m_keys[_row] |= mask;
		else m_keys[_row] &= static_cast<uint16_t>(~mask);
	}

	uint8_t ProE::portAPins() const
	{
		return static_cast<uint8_t>((m_pa7 ? 0x80 : 0) | 0x78 | ((~m_keys[m_keyRow] >> 8) & 7));
	}

	void ProE::turnDial(const int _detents)
	{
		if(!m_valid)
			return;
		m_dialPending += _detents;
		if(!m_dialEvent && m_dialPending)
			m_dialEvent = m_machine.sched().schedule(m_machine.now(), &onDial, this);
	}

	void ProE::onDial(void* _self, const uint64_t _when, uint64_t)
	{
		auto* b = static_cast<ProE*>(_self);
		b->m_dialEvent = 0;
		auto& p = b->m_machine.periph();
		if(b->m_dialPulseLow)
		{
			// The trailing edge of the pulse, then the gap before the next one.
			b->m_dialPulseLow = false;
			b->m_pa7 = true;
			p.set_port_input(m6801::Hc11Peripherals::kPortA, b->portAPins());
			if(b->m_dialPulses || b->m_dialPending)
				b->m_dialEvent = b->m_machine.sched().schedule(_when + g_dialHighStates, &onDial, b);
			return;
		}
		if(!b->m_dialPulses)
		{
			if(!b->m_dialPending)
				return;
			// A new detent: PD2 low turns clockwise (the PAI handler increments), high counter-clockwise.
			const bool up = b->m_dialPending > 0;
			b->m_dialPending += up ? -1 : 1;
			b->m_dialPulses = g_dialPulsesPerDetent;
			p.set_port_input(m6801::Hc11Peripherals::kPortD, up ? 0xfb : 0xff);
		}
		// The falling edge of PA7 counts.
		--b->m_dialPulses;
		b->m_dialPulseLow = true;
		b->m_pa7 = false;
		p.set_port_input(m6801::Hc11Peripherals::kPortA, b->portAPins());
		b->m_dialEvent = b->m_machine.sched().schedule(_when + g_dialLowStates, &onDial, b);
	}

	// ---------------------------------------------------------------------------
	// I/O

	uint8_t ProE::ioRead(const uint32_t _addr)
	{
		if(_addr == 0x1200)
			return static_cast<uint8_t>(~m_keys[m_keyRow] & 0xff);
		if(_addr == 0x1400 || _addr == 0x1401)
			return m_acia.read8(_addr);
		if(_addr == 0x160e)
			return m_lcdRead;
		if(_addr >= 0x1a00 && _addr < 0x1c00)
			return m_keyAssigner[_addr & 0x7ff];
		return 0xff;
	}

	void ProE::ioWrite(const uint32_t _addr, const uint8_t _value)
	{
		switch(_addr)
		{
		case 0x1400: case 0x1401: m_acia.write8(_addr, _value); return;
		case 0x160d: lcdLines(_value); return;
		case 0x160e: m_lcdBus = _value; return;
		case 0x180d:
			selectRomBank(_value);
			m_ramBank = _value & 3;
			return;
		case 0x180e:
			ledIntegrate();
			m_ledLatch = static_cast<uint8_t>(~_value);
			return;
		default:
			if(_addr >= 0x1a00 && _addr < 0x1c00)
				m_keyAssigner[_addr & 0x7ff] = _value;
			return;
		}
	}

	// The time since the last change goes to the LEDs that were lit; a full window decides them.
	// One of six rows gets a sixth of the time, so an LED driven for a twelfth is lit.
	void ProE::ledIntegrate()
	{
		const auto now = m_machine.now();
		if(m_ledLatch)
		{
			const auto elapsed = static_cast<uint32_t>(std::min<uint64_t>(now - m_ledSince, LedWindow));
			for(unsigned bit = 0; bit < 8; ++bit)
				if(m_ledLatch & (1u << bit))
					m_ledTime[m_keyRow][bit] += elapsed;
		}
		m_ledSince = now;
		if(now - m_ledWindowStart < LedWindow)
			return;
		const auto threshold = static_cast<uint32_t>((now - m_ledWindowStart) / 12);
		for(unsigned row = 0; row < 8; ++row)
		{
			uint8_t lit = 0;
			for(unsigned bit = 0; bit < 8; ++bit)
				if(m_ledTime[row][bit] > threshold)
					lit |= static_cast<uint8_t>(1u << bit);
			m_leds[row] = lit;
			m_ledTime[row].fill(0);
		}
		m_ledWindowStart = now;
	}

	// A read cycle happens while E is high with R/W set (the board latches the bus for the
	// following read of 160E); a write cycle on the falling edge of E with R/W clear.
	void ProE::lcdLines(const uint8_t _value)
	{
		const bool eWas = m_lcdLines & 1, e = _value & 1;
		const bool read = _value & 2, rs = _value & 4;
		if(e && !eWas && read)
		{
			if(const auto v = m_lcd.exec(rs, true, 0))
				m_lcdRead = *v;
		}
		const bool readWas = m_lcdLines & 2, rsWas = m_lcdLines & 4;
		if(!e && eWas && !readWas)
			m_lcd.exec(rsWas, false, m_lcdBus);
		m_lcdLines = _value;
	}

	// ---------------------------------------------------------------------------
	// Memory

	void ProE::ramWrite(const uint32_t _addr, uint8_t _value)
	{
		if(_addr == g_protectAddress && m_ramBank == 0 && !m_protectAtPowerOn &&
		   m_machine.cpu().current_pc() == g_powerOnDefaultsPc)
			_value &= static_cast<uint8_t>(~g_protectBit);
		m_ram[ramOffset(_addr)] = _value;
	}

	void ProE::selectRomBank(const uint8_t _latch)
	{
		// A card of 32 KiB lacks the line that picks the half, so it shows in both.
		const bool card = _latch & g_latchCard;
		const bool upperHalf = (_latch & g_latchCardHalf) && m_card.size() > CardBankSize;
		const auto romBank = static_cast<uint8_t>(_latch >> 4);
		const uint8_t bank = card ? (m_card.empty() ? g_codeBankNoCard : static_cast<uint8_t>(g_codeBankCard + upperHalf))
			: romBank < 8         ? romBank
			                      : g_codeBankUnmapped;
		if(bank == m_romBank)
			return;
		m_romBank = bank;
		uint8_t* window = m_machine.bus().mem() + RomWindow;
		if(bank < 8)
			std::memcpy(window, m_patterns.data() + size_t(bank) * RomWindowSize, RomWindowSize);
		else if(bank == g_codeBankNoCard)
			std::memset(window, 0x00, RomWindowSize);
		else if(bank == g_codeBankUnmapped)
			std::memset(window, 0xff, RomWindowSize);
		else
		{
			std::memcpy(window, m_card.data() + size_t(bank - g_codeBankCard) * CardBankSize, RomWindowSize);
			auto& status = window[g_cardStatusOffset];
			status = static_cast<uint8_t>((status & ~g_cardStatusMask) | g_cardPresent);
		}
		// Code runs from the window too.
		m_machine.cpu().select_code_bank(RomWindow, RomWindowSize, bank);
	}
}
