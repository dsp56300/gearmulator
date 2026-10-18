#include "88lib/mt32/mt32.h"

#include "88lib/gs/gs.h"

#include <algorithm>
#include <array>

namespace emu88Lib::mt32
{
	bool hasLaBoard(const DeviceModel _model)
	{
		return isLaModel(_model) || _model == DeviceModel::Cm64;
	}

	bool hasPcmBoard(const DeviceModel _model)
	{
		// The CM-32P's board. Not every board with a PCM card slot: the MV-30 takes the same cards but speaks
		// nothing of this dialect.
		return _model == DeviceModel::Cm32p || _model == DeviceModel::Cm64;
	}

	bool isMt32Model(const DeviceModel _model)
	{
		return hasLaBoard(_model) || hasPcmBoard(_model);
	}

	LaFirmware laFirmware(const DeviceModel _model)
	{
		return _model == DeviceModel::Mt32Old ? LaFirmware::Old : LaFirmware::New;
	}

	bool answersDataRequests(const Board _board)
	{
		return _board == Board::La;
	}

	uint8_t rhythmKeyCount(const LaFirmware _firmware)
	{
		return _firmware == LaFirmware::Old ? 64 : 85;
	}

	namespace
	{
		constexpr std::array<AreaInfo, static_cast<size_t>(Area::Count)> g_areas = {{
			{Area::PatchTemp, Board::La, "Patch Temp", g_patchTemp, g_patchTempSize, g_patchTempSize, g_laParts},
			{Area::RhythmSetup, Board::La, "Rhythm Setup", g_rhythmSetup, g_rhythmKeySize, g_rhythmKeySize, 85},
			{Area::TimbreTemp, Board::La, "Timbre Temp", g_timbreTemp, g_timbreSize, g_timbreSize, g_laTimbreParts},
			{Area::PatchMemory, Board::La, "Patch Memory", g_patchMemory, g_patchMemorySize, g_patchMemorySize,
				g_patchMemoryCount},
			{Area::TimbreMemory, Board::La, "Timbre Memory", g_timbreMemory, g_timbreSize, g_timbreMemoryStride,
				g_timbreMemoryCount},
			{Area::LaSystem, Board::La, "System", g_laSystem, g_laSystemSize, g_laSystemSize, 1},
			{Area::PcmPatchTemp, Board::Pcm, "Patch Temp", g_pcmPatchTemp, g_pcmPatchTempSize, g_pcmPatchTempSize,
				g_pcmParts},
			{Area::PcmPatchMemory, Board::Pcm, "Patch Memory", g_pcmPatchMemory, g_pcmPatchMemorySize,
				g_pcmPatchMemorySize, g_pcmPatchMemoryCount},
			{Area::PcmSystem, Board::Pcm, "System", g_pcmSystem, g_pcmSystemSize, g_pcmSystemSize, 1},
		}};
	}

	const AreaInfo& areaInfo(const Area _area)
	{
		return g_areas[static_cast<size_t>(_area)];
	}

	uint16_t entryCount(const Area _area, const LaFirmware _firmware)
	{
		if(_area == Area::RhythmSetup)
			return rhythmKeyCount(_firmware);
		return areaInfo(_area).count;
	}

	Location locate(const Board _board, const uint32_t _address, const LaFirmware _firmware)
	{
		const auto linear = toLinear(_address);
		for(const auto& a : g_areas)
		{
			if(a.board != _board)
				continue;
			const auto base = toLinear(a.address);
			if(linear < base)
				continue;
			const auto rel = linear - base;
			const auto entry = rel / a.entryStride;
			const auto offset = rel % a.entryStride;
			if(entry >= entryCount(a.area, _firmware) || offset >= a.entrySize)
				continue;
			return {a.area, static_cast<uint16_t>(entry), static_cast<uint16_t>(offset)};
		}
		return {};
	}

	uint32_t address(const Location& _location)
	{
		const auto& a = areaInfo(_location.area);
		return addressAdd(a.address, static_cast<uint32_t>(_location.entry) * a.entryStride + _location.offset);
	}

	uint32_t toLinear(const uint32_t _address)
	{
		return ((_address >> 16) & 0x7f) << 14 | ((_address >> 8) & 0x7f) << 7 | (_address & 0x7f);
	}

	uint32_t fromLinear(const uint32_t _linear)
	{
		return ((_linear >> 14) & 0x7f) << 16 | ((_linear >> 7) & 0x7f) << 8 | (_linear & 0x7f);
	}

	uint32_t addressAdd(const uint32_t _address, const uint32_t _offset)
	{
		return gs::addressAdd(_address, _offset);
	}

	namespace
	{
		Message header(const uint8_t _command, const uint32_t _address, const uint8_t _deviceId)
		{
			return {0xf0, g_rolandId, static_cast<uint8_t>(_deviceId & 0x7f), g_modelId, _command,
				static_cast<uint8_t>((_address >> 16) & 0x7f), static_cast<uint8_t>((_address >> 8) & 0x7f),
				static_cast<uint8_t>(_address & 0x7f)};
		}

		void finish(Message& _m)
		{
			_m.push_back(gs::checksum(_m.data() + 5, _m.size() - 5));
			_m.push_back(0xf7);
		}
	}

	Message dt1(const uint32_t _address, const uint8_t* _data, const size_t _size, const uint8_t _deviceId)
	{
		auto m = header(g_cmdDt1, _address, _deviceId);
		m.reserve(m.size() + _size + 2);
		for(size_t i = 0; i < _size; ++i)
			m.push_back(_data[i] & 0x7f);
		finish(m);
		return m;
	}

	Message dt1(const uint32_t _address, const std::vector<uint8_t>& _data, const uint8_t _deviceId)
	{
		return dt1(_address, _data.data(), _data.size(), _deviceId);
	}

	std::vector<Message> dt1Chunks(const uint32_t _address, const uint8_t* _data, const size_t _size,
		const uint8_t _deviceId)
	{
		std::vector<Message> messages;
		for(size_t pos = 0; pos < _size; pos += g_maxDt1Size)
		{
			const auto n = std::min(g_maxDt1Size, _size - pos);
			messages.push_back(dt1(addressAdd(_address, static_cast<uint32_t>(pos)), _data + pos, n, _deviceId));
		}
		return messages;
	}

	Message rq1(const uint32_t _address, const uint32_t _size, const uint8_t _deviceId)
	{
		auto m = header(g_cmdRq1, _address, _deviceId);
		m.push_back(static_cast<uint8_t>((_size >> 14) & 0x7f));
		m.push_back(static_cast<uint8_t>((_size >> 7) & 0x7f));
		m.push_back(static_cast<uint8_t>(_size & 0x7f));
		finish(m);
		return m;
	}

	bool parseDt1(const uint8_t* _sysex, const size_t _size, Dt1& _result)
	{
		// F0 41 dev 16 12 a a a d... sum F7
		if(_size < 11 || _sysex[0] != 0xf0 || _sysex[_size - 1] != 0xf7)
			return false;
		if(_sysex[1] != g_rolandId || _sysex[3] != g_modelId || _sysex[4] != g_cmdDt1)
			return false;
		if(gs::checksum(_sysex + 5, _size - 7) != _sysex[_size - 2])
			return false;
		_result.deviceId = _sysex[2];
		_result.address = static_cast<uint32_t>(_sysex[5]) << 16 | static_cast<uint32_t>(_sysex[6]) << 8 | _sysex[7];
		_result.data = _sysex + 8;
		_result.size = _size - 10;
		return true;
	}
}
