#include "88lib/gs/gs.h"

namespace emu88Lib::gs
{
	uint8_t checksum(const uint8_t* _data, const size_t _size)
	{
		uint32_t sum = 0;
		for(size_t i = 0; i < _size; ++i)
			sum += _data[i];
		return static_cast<uint8_t>((0x80 - (sum & 0x7f)) & 0x7f);
	}

	namespace
	{
		Message header(const uint8_t _command, const uint32_t _address)
		{
			return {0xf0, g_rolandId, g_deviceId, g_modelId, _command,
				static_cast<uint8_t>((_address >> 16) & 0x7f), static_cast<uint8_t>((_address >> 8) & 0x7f),
				static_cast<uint8_t>(_address & 0x7f)};
		}

		void finish(Message& _m)
		{
			_m.push_back(checksum(_m.data() + 5, _m.size() - 5));
			_m.push_back(0xf7);
		}
	}

	Message dt1(const uint32_t _address, const std::vector<uint8_t>& _data)
	{
		auto m = header(g_cmdDt1, _address);
		for(const auto d : _data)
			m.push_back(d & 0x7f);
		finish(m);
		return m;
	}

	Message dt1(const uint32_t _address, const uint8_t _value)
	{
		return dt1(_address, std::vector<uint8_t>{_value});
	}

	Message rq1(const uint32_t _address, const uint32_t _size)
	{
		auto m = header(g_cmdRq1, _address);
		m.push_back(static_cast<uint8_t>((_size >> 14) & 0x7f));
		m.push_back(static_cast<uint8_t>((_size >> 7) & 0x7f));
		m.push_back(static_cast<uint8_t>(_size & 0x7f));
		finish(m);
		return m;
	}

	uint8_t partBlock(const uint8_t _part)
	{
		if(_part == 9)
			return 0;
		if(_part < 9)
			return static_cast<uint8_t>(_part + 1);
		return static_cast<uint8_t>(_part & 0x0f);
	}

	uint8_t blockPart(const uint8_t _block)
	{
		const auto block = static_cast<uint8_t>(_block & 0x0f);
		if(block == 0)
			return 9;
		if(block <= 9)
			return static_cast<uint8_t>(block - 1);
		return block;
	}

	uint32_t partAddress(const uint8_t _part, const PartParam _param)
	{
		return 0x401000u | (static_cast<uint32_t>(partBlock(_part)) << 8) | static_cast<uint8_t>(_param);
	}

	uint32_t partAddress(const uint8_t _part, const PartMapParam _param)
	{
		return 0x404000u | (static_cast<uint32_t>(partBlock(_part)) << 8) | static_cast<uint8_t>(_param);
	}

	uint8_t paramSize(const PartParam _param)
	{
		return _param == PartParam::ToneNumber ? 2 : 1;
	}

	uint32_t addressAdd(const uint32_t _address, const uint32_t _offset)
	{
		const auto linear = ((_address >> 16) & 0x7f) << 14 | ((_address >> 8) & 0x7f) << 7 | (_address & 0x7f);
		const auto sum = linear + _offset;
		return ((sum >> 14) & 0x7f) << 16 | ((sum >> 7) & 0x7f) << 8 | (sum & 0x7f);
	}

	bool parseDt1(const uint8_t* _sysex, const size_t _size, Dt1& _result)
	{
		// F0 41 dev 42 12 a a a d... sum F7
		if(_size < 11 || _sysex[0] != 0xf0 || _sysex[_size - 1] != 0xf7)
			return false;
		if(_sysex[1] != g_rolandId || _sysex[3] != g_modelId || _sysex[4] != g_cmdDt1)
			return false;
		if(checksum(_sysex + 5, _size - 7) != _sysex[_size - 2])
			return false;
		_result.address = static_cast<uint32_t>(_sysex[5]) << 16 | static_cast<uint32_t>(_sysex[6]) << 8 | _sysex[7];
		_result.data = _sysex + 8;
		_result.size = _size - 10;
		return true;
	}
}
