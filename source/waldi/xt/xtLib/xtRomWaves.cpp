#include "xtRomWaves.h"

#include <cstdint>

#include "xtState.h"

namespace xt
{
	constexpr uint32_t g_rwRomWaveStartAddr = 0x26501;

	namespace
	{
		bool isValidRwRomWave(const WaveId _id)
		{
			return _id.rawId() >= wave::g_firstRwRomWaveIndex && _id.rawId() < wave::g_romWaveCount;
		}
		uint32_t getWaveAddr(const WaveId _id)
		{
			return g_rwRomWaveStartAddr + (_id.rawId() - wave::g_firstRwRomWaveIndex) * 128;
		}
	}

	bool RomWaves::receiveSysEx(wLib::Responses& _results, const wLib::SysEx& _data) const
	{
		switch (State::getCommand(_data))
		{
		case SysexCommand::WaveRequest:
			{
				auto waveId = State::getWaveId(_data);
				if (!isValidRwRomWave(waveId))
					return false;
				WaveData waveData;
				if (!readFromRom(waveData, waveId))
					return false;
				auto response = State::createWaveData(waveData, waveId.rawId(), false);
				_results.emplace_back(std::move(response));
			}
			return true;
		case SysexCommand::WaveDump:
			{
				auto waveId = State::getWaveId(_data);
				if (!isValidRwRomWave(waveId))
					return false;

				WaveData d;
				if (!State::parseWaveData(d, _data))
					return false;

				writeToRom(waveId, d);
			}
			return true;
		default:
			return false;
		}
	}

	bool RomWaves::writeToRom(const WaveId _id, const WaveData& _waveData) const
	{
		if (!isValidRwRomWave(_id))
			return false;

		const auto addr = getWaveAddr(_id);

		for (size_t i=0; i<64; ++i)
		{
			const auto idx = i << 1;
			const auto sample = static_cast<uint8_t>(_waveData[i] ^ 0x80);

			m_uc.getRomRuntimeData()[addr + idx] = sample;
		}

		return true;
	}

	bool RomWaves::readFromRom(WaveData& _waveData, const WaveId _id) const
	{
		if (!isValidRwRomWave(_id))
			return false;

		const auto addr = getWaveAddr(_id);
		for (size_t i = 0; i < 64; ++i)
		{
			const auto idx = i << 1;
			const auto sample = m_uc.getRomRuntimeData()[addr + idx] ^ 0x80;
			_waveData[i] = static_cast<int8_t>(sample);
			_waveData[127-i] = static_cast<int8_t>(-sample);
		}
		return true;
	}
}
