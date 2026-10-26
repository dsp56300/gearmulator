#pragma once

#include <cstddef>
#include <cstdint>

// Roland exclusive framing: F0 41 <device> <model id...> <command> <address...> <data | size...> <checksum> F7.
// The model id is one byte on most devices and two on later ones (JP-8000 00 06); addresses
// and RQ1 sizes are three or four 7-bit digits. The checksum covers everything between the command byte and
// itself.
namespace rLib::sysex
{
	constexpr uint8_t Sox = 0xf0;
	constexpr uint8_t Eox = 0xf7;
	constexpr uint8_t ManufacturerRoland = 0x41;
	constexpr uint8_t CmdRQ1 = 0x11;
	constexpr uint8_t CmdDT1 = 0x12;

	struct Model
	{
		uint8_t id[2];
		uint8_t idSize;
		uint8_t addressSize;

		constexpr size_t headerSize() const { return 4u + idSize; }	// F0 41 dev <id> cmd
	};

	inline uint8_t checksum(const uint8_t* _data, const size_t _size)
	{
		uint32_t sum = 0;
		for(size_t i = 0; i < _size; ++i)
			sum += _data[i];
		return static_cast<uint8_t>((0x80u - (sum & 0x7fu)) & 0x7fu);
	}

	// A value packed as 7 bits per digit, most significant digit first.
	inline void toDigits(uint8_t* _dst, const uint32_t _value, const uint32_t _count)
	{
		for(uint32_t i = 0; i < _count; ++i)
			_dst[i] = static_cast<uint8_t>((_value >> (7 * (_count - 1 - i))) & 0x7f);
	}

	inline uint32_t fromDigits(const uint8_t* _src, const uint32_t _count)
	{
		uint32_t v = 0;
		for(uint32_t i = 0; i < _count; ++i)
			v = (v << 7) | (_src[i] & 0x7f);
		return v;
	}

	template<typename Buffer> void appendHeader(Buffer& _dst, const Model& _model, const uint8_t _device, const uint8_t _command)
	{
		_dst.push_back(Sox);
		_dst.push_back(ManufacturerRoland);
		_dst.push_back(_device);
		_dst.insert(_dst.end(), _model.id, _model.id + _model.idSize);
		_dst.push_back(_command);
	}

	// Checksum over [_checksumBegin, end) followed by F7.
	template<typename Buffer> void appendFooter(Buffer& _dst, const size_t _checksumBegin)
	{
		_dst.push_back(checksum(_dst.data() + _checksumBegin, _dst.size() - _checksumBegin));
		_dst.push_back(Eox);
	}

	template<typename Buffer> Buffer buildDT1(const Model& _model, const uint8_t _device, const uint8_t* _address,
		const uint8_t* _data, const size_t _size)
	{
		Buffer b;
		b.reserve(_model.headerSize() + _model.addressSize + _size + 2);
		appendHeader(b, _model, _device, CmdDT1);
		b.insert(b.end(), _address, _address + _model.addressSize);
		if(_data && _size)
			b.insert(b.end(), _data, _data + _size);
		appendFooter(b, _model.headerSize());
		return b;
	}

	template<typename Buffer> Buffer buildDT1(const Model& _model, const uint8_t _device, const uint32_t _address,
		const uint8_t* _data, const size_t _size)
	{
		uint8_t a[4];
		toDigits(a, _address, _model.addressSize);
		return buildDT1<Buffer>(_model, _device, a, _data, _size);
	}

	template<typename Buffer> Buffer buildRQ1(const Model& _model, const uint8_t _device, const uint8_t* _address,
		const uint8_t* _size)
	{
		Buffer b;
		b.reserve(_model.headerSize() + _model.addressSize * 2 + 2);
		appendHeader(b, _model, _device, CmdRQ1);
		b.insert(b.end(), _address, _address + _model.addressSize);
		b.insert(b.end(), _size, _size + _model.addressSize);
		appendFooter(b, _model.headerSize());
		return b;
	}

	template<typename Buffer> Buffer buildRQ1(const Model& _model, const uint8_t _device, const uint32_t _address,
		const uint32_t _size)
	{
		uint8_t a[4];
		uint8_t s[4];
		toDigits(a, _address, _model.addressSize);
		toDigits(s, _size, _model.addressSize);
		return buildRQ1<Buffer>(_model, _device, a, s);
	}

	// F0 41 <any device> <model id> <_command>, no length, terminator or checksum check.
	inline bool matchHeader(const uint8_t* _msg, const size_t _size, const Model& _model, const uint8_t _command)
	{
		if(_size < _model.headerSize() || _msg[0] != Sox || _msg[1] != ManufacturerRoland)
			return false;
		for(uint8_t i = 0; i < _model.idSize; ++i)
		{
			if(_msg[3 + i] != _model.id[i])
				return false;
		}
		return _msg[3 + _model.idSize] == _command;
	}

	// Checksum of a complete message, excluding framing.
	inline bool checksumValid(const uint8_t* _msg, const size_t _size, const Model& _model)
	{
		const auto begin = _model.headerSize();
		if(_size < begin + 2)
			return false;
		return checksum(_msg + begin, _size - begin - 2) == _msg[_size - 2];
	}

	struct Rq1View
	{
		const uint8_t* address = nullptr;
		const uint8_t* size = nullptr;
	};

	// An RQ1 of _model, terminated by F7. The checksum is not verified.
	inline bool parseRQ1(const uint8_t* _msg, const size_t _size, const Model& _model, Rq1View& _out)
	{
		const auto sizeBegin = _model.headerSize() + _model.addressSize;
		if(_size != sizeBegin + _model.addressSize + 2 || _msg[_size - 1] != Eox || !matchHeader(_msg, _size, _model, CmdRQ1))
			return false;
		_out.address = _msg + _model.headerSize();
		_out.size = _msg + sizeBegin;
		return true;
	}

	struct Dt1View
	{
		const uint8_t* address = nullptr;
		const uint8_t* data = nullptr;
		size_t dataSize = 0;
	};

	// A DT1 of _model with at least an address, terminated by F7. The checksum is not verified.
	inline bool parseDT1(const uint8_t* _msg, const size_t _size, const Model& _model, Dt1View& _out)
	{
		const auto dataBegin = _model.headerSize() + _model.addressSize;
		if(_size < dataBegin + 2 || _msg[_size - 1] != Eox || !matchHeader(_msg, _size, _model, CmdDT1))
			return false;
		_out.address = _msg + _model.headerSize();
		_out.data = _msg + dataBegin;
		_out.dataSize = _size - dataBegin - 2;
		return true;
	}
}
