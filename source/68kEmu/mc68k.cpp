#include "mc68k.h"

namespace mc68k
{
	Mc68k::Mc68k()
	{
	}

	Mc68k::~Mc68k()
	{
	}

	void Mc68k::writeW(std::vector<uint8_t>& _buf, size_t _offset, uint16_t _value)
	{
		_buf[_offset] = _value >> 8;
		_buf[_offset+1] = _value & 0xff;
	}

	uint16_t Mc68k::readW(const std::vector<uint8_t>& _buf, size_t _offset)
	{
		const uint16_t a = _buf[_offset];
		const uint16_t b = _buf[_offset+1];

		return a << 8 | b;
	}
}
