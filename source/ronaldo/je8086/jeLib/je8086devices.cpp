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

	Faders::Faders(h8300h::Machine& _machine, const ChannelMap& _channels)
	: m_channels(_channels)
	, m_adc(_machine, [this](const unsigned _channel)
	{
		// [AN8-4 / AN3-0][register pair][mux select]
		return m_values[m_channels[((_channel << 3) | (m_port6 & 7)) & 63]];
	})
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
		m_port6 = 0;
		m_adc.reset();
	}

	uint8_t Faders::read8(const uint32_t _address)
	{
		if (_address == 0xffffcb)
			return static_cast<uint8_t>(m_port6);
		return m_adc.read8(_address);
	}

	void Faders::write8(const uint32_t _address, const uint8_t _value)
	{
		if (_address == 0xffffcb)
			m_port6 = static_cast<int8_t>(_value);
		else
			m_adc.write8(_address, _value);
	}

	void Faders::setFader(const int _which, const int _value)
	{
		if (_which >= 0 && _which < static_cast<int>(m_values.size()))
			m_values[_which] = std::clamp(_value, 0, 1023);
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
