#include "frontPanelSysex.h"

#include <algorithm>

namespace synthLib
{
	namespace fp
	{
		namespace
		{
			SysexBuffer& begin(std::vector<SMidiEvent>& _dst, const Type _type)
			{
				auto& sysex = _dst.emplace_back(MidiEventSource::Internal).sysex;
				sysex.assign(std::begin(g_header), std::end(g_header));
				sysex.push_back(static_cast<uint8_t>(_type));
				return sysex;
			}

			void end(SysexBuffer& _sysex)
			{
				_sysex.push_back(0xf7);
			}
		}

		bool isFrontPanel(const SysexBuffer& _sysex)
		{
			return _sysex.size() >= g_headerSize + 2 && std::equal(std::begin(g_header), std::end(g_header), _sysex.begin()) && _sysex.back() == 0xf7;
		}

		void pack7(SysexBuffer& _dst, const uint8_t* _src, const size_t _count)
		{
			for(size_t g = 0; g < _count; g += 7)
			{
				uint8_t msbs = 0;
				for(size_t k = 0; k < 7; ++k)
				{
					const uint8_t b = g + k < _count ? _src[g + k] : 0;
					_dst.push_back(b & 0x7f);
					msbs |= static_cast<uint8_t>((b >> 7) << k);
				}
				_dst.push_back(msbs);
			}
		}

		size_t packedSize(const size_t _count)
		{
			return (_count + 6) / 7 * 8;
		}

		void unpack7(std::vector<uint8_t>& _dst, const uint8_t* _src, const size_t _count)
		{
			_dst.resize(_count);
			for(size_t i = 0; i < _count; ++i)
			{
				const auto* group = _src + i / 7 * 8;
				const auto k = i % 7;
				_dst[i] = static_cast<uint8_t>((group[k] & 0x7f) | ((group[7] >> k) & 1) << 7);
			}
		}

		void pushNibbles(SysexBuffer& _dst, const uint64_t _value, const uint32_t _count)
		{
			for(uint32_t i = _count; i-- > 0;)
				_dst.push_back(static_cast<uint8_t>((_value >> (i * 4)) & 0xf));
		}

		uint64_t readNibbles(const uint8_t* _src, const uint32_t _count)
		{
			uint64_t v = 0;
			for(uint32_t i = 0; i < _count; ++i)
				v = (v << 4) | (_src[i] & 0xf);
			return v;
		}

		void sendLcdText(std::vector<SMidiEvent>& _dst, const uint32_t _cols, const uint32_t _rows, const uint8_t* _cells)
		{
			auto& s = begin(_dst, Type::LcdText);
			s.push_back(static_cast<uint8_t>(_cols & 0x7f));
			s.push_back(static_cast<uint8_t>(_rows & 0x7f));
			pack7(s, _cells, static_cast<size_t>(_cols) * _rows);
			end(s);
		}

		void sendLcdCgRam(std::vector<SMidiEvent>& _dst, const std::array<uint8_t, g_cgRamSize>& _cgRam)
		{
			auto& s = begin(_dst, Type::LcdCgRam);
			// a character row has 5 pixels, the HD44780 ignores the rest
			for(const auto b : _cgRam)
				s.push_back(b & 0x1f);
			end(s);
		}

		void sendLedMask(std::vector<SMidiEvent>& _dst, const std::vector<bool>& _leds)
		{
			auto& s = begin(_dst, Type::LedMask);
			s.push_back(static_cast<uint8_t>(_leds.size() & 0x7f));
			for(size_t i = 0; i < _leds.size(); i += 7)
			{
				uint8_t bits = 0;
				for(size_t k = 0; k < 7 && i + k < _leds.size(); ++k)
				{
					if(_leds[i + k])
						bits |= static_cast<uint8_t>(1 << k);
				}
				s.push_back(bits);
			}
			end(s);
		}

		void sendButton(std::vector<SMidiEvent>& _dst, const uint32_t _index, const bool _pressed)
		{
			auto& s = begin(_dst, Type::Button);
			pushNibbles(s, _index, 4);
			s.push_back(_pressed ? 1 : 0);
			end(s);
		}

		void sendEncoder(std::vector<SMidiEvent>& _dst, const uint32_t _index, const int32_t _delta)
		{
			auto& s = begin(_dst, Type::Encoder);
			s.push_back(static_cast<uint8_t>(_index & 0x7f));
			s.push_back(static_cast<uint8_t>(std::clamp(_delta + 64, 0, 127)));
			end(s);
		}

		void sendParam(std::vector<SMidiEvent>& _dst, const uint8_t _page, const uint8_t _index, const int32_t _value)
		{
			auto& s = begin(_dst, Type::Param);
			pushNibbles(s, _page, 2);
			pushNibbles(s, _index, 2);
			pushNibbles(s, static_cast<uint32_t>(_value), 8);
			end(s);
		}

		void sendRequestState(std::vector<SMidiEvent>& _dst, const uint8_t _mask)
		{
			auto& s = begin(_dst, Type::RequestState);
			s.push_back(_mask & 0x7f);
			end(s);
		}
	}

	bool FrontPanelTransport::receive(const SysexBuffer& _sysex)
	{
		if(!fp::isFrontPanel(_sysex))
			return false;

		const auto type = _sysex[fp::g_typeIndex];
		const uint8_t* p = _sysex.data() + fp::g_typeIndex + 1;
		const size_t size = _sysex.size() - fp::g_typeIndex - 2;	// payload, without type and F7

		switch(static_cast<fp::Type>(type))
		{
		case fp::Type::LcdText:
			{
				if(size < 2)
					return true;
				const uint32_t cols = p[0];
				const uint32_t rows = p[1];
				const size_t count = static_cast<size_t>(cols) * rows;
				if(size != 2 + fp::packedSize(count))
					return true;
				std::vector<uint8_t> cells;
				fp::unpack7(cells, p + 2, count);
				evLcdText.retain(cols, rows, std::move(cells));
			}
			return true;
		case fp::Type::LcdCgRam:
			{
				if(size != fp::g_cgRamSize)
					return true;
				std::array<uint8_t, fp::g_cgRamSize> cg{};
				std::copy_n(p, cg.size(), cg.begin());
				evLcdCgRam.retain(cg);
			}
			return true;
		case fp::Type::LedMask:
			{
				if(size < 1)
					return true;
				const size_t count = p[0];
				if(size != 1 + (count + 6) / 7)
					return true;
				std::vector<bool> leds(count);
				for(size_t i = 0; i < count; ++i)
					leds[i] = (p[1 + i / 7] >> (i % 7)) & 1;
				evLedMask.retain(std::move(leds));
			}
			return true;
		case fp::Type::Button:
			if(size == 5)
				evButton(static_cast<uint32_t>(fp::readNibbles(p, 4)), p[4] != 0);
			return true;
		case fp::Type::Encoder:
			if(size == 2)
				evEncoder(p[0], static_cast<int32_t>(p[1]) - 64);
			return true;
		case fp::Type::Param:
			if(size == 12)
			{
				const auto page = static_cast<uint8_t>(fp::readNibbles(p, 2));
				const auto index = static_cast<uint8_t>(fp::readNibbles(p + 2, 2));
				const auto value = static_cast<int32_t>(static_cast<uint32_t>(fp::readNibbles(p + 4, 8)));
				evParam(page, index, value);
			}
			return true;
		case fp::Type::RequestState:
			if(size == 1)
				evRequestState(p[0]);
			return true;
		default:
			if(type >= static_cast<uint8_t>(fp::Type::ProductFirst) && type <= static_cast<uint8_t>(fp::Type::ProductLast))
				evProduct(type, _sysex);
			return true;
		}
	}
}
