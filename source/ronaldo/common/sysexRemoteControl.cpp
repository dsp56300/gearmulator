#include "sysexRemoteControl.h"

#include <algorithm>

#include "hardwareLib/hd44780.h"

namespace rLib
{
	static_assert(hwLib::Hd44780::CgRamSize == SysexRemoteControl::CgRamSize, "CGRAM transport size out of sync with the LCD model");
	static_assert(hwLib::Hd44780::DdRamSize == SysexRemoteControl::DdRamSize, "DDRAM transport size out of sync with the LCD model");

	SysexRemoteControl::SysexRemoteControl(const std::initializer_list<uint8_t> _prefix) : m_prefix(_prefix)
	{
	}

	bool SysexRemoteControl::receive(const synthLib::SMidiEvent& _input) const
	{
		return !_input.sysex.empty() && receive(_input.sysex);
	}

	bool SysexRemoteControl::receive(const synthLib::SysexBuffer& _input) const
	{
		const size_t headerSize = 2 + m_prefix.size() + 1;

		if(_input.size() < headerSize + 1 || _input[0] != 0xf0 || _input[1] != ManufacturerId || _input.back() != 0xf7)
			return false;

		if(!std::equal(m_prefix.begin(), m_prefix.end(), _input.begin() + 2))
			return false;

		return onCommand(_input[headerSize - 1], _input.data() + headerSize, _input.size() - headerSize - 1);
	}

	synthLib::SysexBuffer& SysexRemoteControl::begin(std::vector<synthLib::SMidiEvent>& _dst, const std::initializer_list<uint8_t> _header)
	{
		auto& sysex = _dst.emplace_back(synthLib::MidiEventSource::Internal).sysex;
		sysex.assign({0xf0, ManufacturerId});
		sysex.insert(sysex.end(), _header.begin(), _header.end());
		return sysex;
	}

	void SysexRemoteControl::pushNibbles(synthLib::SysexBuffer& _dst, const uint64_t _value, const uint32_t _count)
	{
		for(uint32_t i = _count; i-- > 0;)
			_dst.push_back(static_cast<uint8_t>((_value >> (i * 4)) & 0xf));
	}

	uint64_t SysexRemoteControl::readNibbles(const uint8_t* _src, const uint32_t _count)
	{
		uint64_t v = 0;
		for(uint32_t i = 0; i < _count; ++i)
			v = (v << 4) | (_src[i] & 0xf);
		return v;
	}

	void SysexRemoteControl::pushLcdCgRam(synthLib::SysexBuffer& _dst, const hwLib::Hd44780& _lcd)
	{
		const auto& cg = _lcd.getCgRam();
		_dst.insert(_dst.end(), cg.begin(), cg.end());
	}

	void SysexRemoteControl::pushLcdDdRam(synthLib::SysexBuffer& _dst, const hwLib::Hd44780& _lcd)
	{
		const auto& dd = _lcd.getDdRam();
		_dst.insert(_dst.end(), dd.begin(), dd.end());
	}

	void SysexRemoteControl::pushLcdVisibleDdRam(synthLib::SysexBuffer& _dst, const hwLib::Hd44780& _lcd)
	{
		std::array<char, VisibleDdRamSize> visible{};
		_lcd.copyVisibleDdRam(visible.data());
		for(const auto c : visible)
			_dst.push_back(static_cast<uint8_t>(c));
	}

	void SysexRemoteControl::pushLcdCursor(synthLib::SysexBuffer& _dst, const hwLib::Hd44780& _lcd)
	{
		_dst.push_back(_lcd.isDisplayOn() ? 1 : 0);
		_dst.push_back(_lcd.isCursorOn() ? 1 : 0);
		_dst.push_back(_lcd.isCursorBlinking() ? 1 : 0);
		_dst.push_back(static_cast<uint8_t>(_lcd.getCursorAddress() & 0x7f));
	}

	SysexRemoteControl::LcdCursorState SysexRemoteControl::readLcdCursor(const uint8_t* _src)
	{
		return {_src[0] != 0, _src[1] != 0, _src[2] != 0, static_cast<uint8_t>(_src[3] & 0x7f)};
	}

	void SysexRemoteControl::pushLcdVram(synthLib::SysexBuffer& _dst, const uint16_t _width, const uint16_t _height,
		const std::vector<uint8_t>& _mono)
	{
		pushNibbles(_dst, _width, 3);
		pushNibbles(_dst, _height, 3);

		const size_t pixels = static_cast<size_t>(_width) * _height;
		const size_t count = std::min(pixels, _mono.size());
		_dst.reserve(_dst.size() + ((pixels + 7) / 8) * 2 + 1);

		uint8_t packed = 0;
		for(size_t i = 0; i < pixels; ++i)
		{
			const uint8_t bit = i & 7;
			if(i < count && _mono[i])
				packed |= static_cast<uint8_t>(1u << bit);
			if(bit == 7 || i + 1 == pixels)
			{
				_dst.push_back(packed & 0xf);
				_dst.push_back((packed >> 4) & 0xf);
				packed = 0;
			}
		}
	}

	const std::vector<uint8_t>* SysexRemoteControl::readLcdVram(const uint8_t* _src, const size_t _size, uint16_t& _width,
		uint16_t& _height) const
	{
		if(_size < 6)
			return nullptr;

		_width = static_cast<uint16_t>(readNibbles(_src, 3));
		_height = static_cast<uint16_t>(readNibbles(_src + 3, 3));

		const size_t pixels = static_cast<size_t>(_width) * _height;
		if(_size != 6 + ((pixels + 7) / 8) * 2)
			return nullptr;

		m_lcdScratch.assign(pixels, 0);
		const uint8_t* src = _src + 6;
		for(size_t px = 0; px < pixels;)
		{
			const auto packed = static_cast<uint8_t>((src[0] & 0xf) | ((src[1] & 0xf) << 4));
			src += 2;
			for(uint8_t bit = 0; bit < 8 && px < pixels; ++bit, ++px)
				m_lcdScratch[px] = (packed >> bit) & 1;
		}
		return &m_lcdScratch;
	}
}
