#include "je8086devices.h"

#include "dsp56kBase/logging.h"

namespace jeLib::devices
{
	Port::Port(const uint32_t _rowCount) : m_rowCount(_rowCount)
	{
		releaseAll();
	}

	void Port::reset()
	{
		m_leds.fill(0);
		m_portA = m_portBDdr = m_portB = 0;
		m_latchArmed = false;
		releaseAll();
	}

	void Port::releaseAll()
	{
		m_switches.fill(static_cast<int8_t>(0xff));
	}

	void Port::press(uint32_t _which, const bool _down)
	{
		const auto bit = static_cast<int8_t>(1u << (_which & 7));
		_which >>= 3;
		if (_which >= m_rowCount)
			return;
		if (_down)
			m_switches[_which] &= static_cast<int8_t>(~bit);
		else
			m_switches[_which] |= bit;
	}

	void Port::setTpcOutput(const uint16_t _value, const uint16_t _mask)
	{
		m_portA = static_cast<int8_t>((static_cast<uint8_t>(m_portA) & ~_mask) | (_value & _mask));
		m_portB = static_cast<int8_t>((static_cast<uint8_t>(m_portB) & ~(_mask >> 8)) | ((_value >> 8) & (_mask >> 8)));
	}

	uint8_t Port::read8(const uint32_t _address)
	{
		if (_address == 0xffffd4)
			return m_portBDdr;
		return _address == 0xffffd3 ? m_portA : m_switches[static_cast<uint8_t>(m_portA) & (m_rowCount - 1)];
	}

	void Port::write8(const uint32_t _address, const uint8_t _value)
	{
		if (_address == 0xffffd4)
		{
			m_portBDdr = static_cast<int8_t>(_value);
			return;
		}
		if (_address != 0xffffd3)
		{
			m_portB = static_cast<int8_t>(_value);
			return;
		}
		// LEDs latch port B on the falling edge of PA5
		if (_value & 32)
		{
			m_latchArmed = true;
		}
		else if (m_latchArmed)
		{
			m_leds[_value & 31] = m_portB;
			m_latchArmed = false;
		}
		m_portA = static_cast<int8_t>(_value);
	}

	Faders::Faders(h8300h::Machine& _machine, const ChannelMap& _channels) : m_machine(_machine), m_channels(_channels)
	{
		m_values.fill(512);
	}

	Faders::ChannelMap Faders::keyboardChannels()
	{
		ChannelMap map{};
		for (uint8_t c = 0; c < map.size(); ++c)
		{
			uint8_t which = c;
			if ((which & 0x38) == 0) which = kFader_PitchBend;		// AN0 carries pitch bend on every mux input
			if ((which & 0x38) == 8) which &= 0x3b;					// mod / expression / battery ignore mux bit 2
			if ((which & 0x3c) == 40) which = kFader_Osc1Ctrl2;		// wired to several mux inputs
			if ((which & 0x3e) == 44) which = kFader_Osc1Ctrl1;
			if ((which & 0x3e) == 46) which = kFader_FineTune;
			map[c] = which;
		}
		return map;
	}

	void Faders::reset()
	{
		cancelConversion();
		m_scanning = 0;
		m_port6 = 0;
		m_adcsr = 0;
		m_adcr = 0x7f;
		m_adfRead = false;
		updateInterruptRequest();
	}

	uint8_t Faders::read8(const uint32_t _address)
	{
		if (_address == 0xffffcb)
			return static_cast<uint8_t>(m_port6);
		if (_address == 0xffffe8)
		{
			settleConversion();
			if (m_adcsr & 0x80)
				m_adfRead = true;
			return m_adcsr;
		}
		if (_address == 0xffffe9)
			return m_adcr;
		if (_address < 0xffffe0 || _address >= 0xffffe8)
			return 0;

		const int offset = (_address - 0xffffe0) & 7;
		const int channel = m_scanning | ((offset << 2) & 0x18);	// [AN8-4 / AN3-0][register pair][mux select]
		const int value = m_values[m_channels[channel & 63]] & 1023;
		return (offset & 1) ? static_cast<uint8_t>((value << 6) & 0xc0) : static_cast<uint8_t>(value >> 2);
	}

	void Faders::write8(const uint32_t _address, const uint8_t _value)
	{
		if (_address == 0xffffcb)
		{
			m_scanning = static_cast<int8_t>((_value & 7) | (m_scanning & 32));
			m_port6 = static_cast<int8_t>(_value);
		}
		else if (_address == 0xffffe8)
		{
			m_scanning = static_cast<int8_t>((m_scanning & 7) | ((_value << 3) & 32));
			settleConversion();
			// ADF clears only when written 0 after having been read as 1
			const bool adf = (m_adcsr & 0x80) && (!m_adfRead || (_value & 0x80));
			m_adcsr = static_cast<uint8_t>((_value & 0x7f) | (adf ? 0x80 : 0));
			m_adfRead = false;
			cancelConversion();
			m_conversionStart = m_machine.now();
			if ((m_adcsr & 0x60) == 0x60)
				scheduleConversion(m_conversionStart);
			updateInterruptRequest();
		}
		else if (_address == 0xffffe9)
		{
			m_adcr = _value | 0x7f;
		}
	}

	void Faders::setFader(const int _which, const int _value)
	{
		if (_which >= 0 && _which < static_cast<int>(m_values.size()))
			m_values[_which] = std::clamp(_value, 0, 1023);
	}

	void Faders::scheduleConversion(const uint64_t _start)
	{
		m_conversionStart = _start;
		m_conversionEvent = m_machine.sched().schedule(_start + duration(), &onConversion, this);
	}

	void Faders::cancelConversion()
	{
		if (!m_conversionEvent)
			return;
		m_machine.sched().cancel(m_conversionEvent);
		m_conversionEvent = 0;
	}

	// Without ADIE nothing observes a conversion until ADCSR is accessed, so it is resolved then
	// instead of waking the scheduler for every conversion of a scan.
	void Faders::settleConversion()
	{
		if (m_conversionEvent || !(m_adcsr & 0x20))
			return;
		const uint64_t now = m_machine.now();
		const uint64_t length = duration();
		if (now < m_conversionStart + length)
			return;
		m_adcsr |= 0x80;
		if (m_adcsr & 0x10)
			m_conversionStart += (now - m_conversionStart) / length * length;
		else
			m_adcsr &= ~0x20;
	}

	void Faders::onConversion(void* _self, uint64_t, uint64_t)
	{
		auto* self = static_cast<Faders*>(_self);
		self->m_conversionEvent = 0;
		self->m_adcsr |= 0x80;
		if (self->m_adcsr & 0x10)
			self->scheduleConversion(self->m_conversionStart + self->duration());
		else
			self->m_adcsr &= ~0x20;
		self->updateInterruptRequest();
	}

	void Faders::updateInterruptRequest()
	{
		m_machine.periph().set_level(h8300h::kVecAdi, (m_adcsr & 0xc0) == 0xc0);
	}

	uint8_t CatchAllDevice::read8(const uint32_t _address)
	{
		report("read", _address);
		return 0;
	}

	void CatchAllDevice::write8(const uint32_t _address, uint8_t)
	{
		report("write", _address);
	}

	void CatchAllDevice::report(const char* _access, const uint32_t _address)
	{
		if (m_reported.insert(_address).second)
			LOG("Unmapped " << _access << " at " << HEXN(_address, 6));
	}
}
